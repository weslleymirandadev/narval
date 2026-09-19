// ExplainOwnershipPass — the source-level ownership report behind --explain-ownership.
//
// The drops pass decides; this pass tells the story. It runs right after it (end of
// phase A) and reads the module back as events the person who wrote the .nv can
// recognise: every event is anchored on a source line and names the value the way the
// source does, instead of printing the C bridge that happens to implement it.
//
// Vocabulary (OWNERSHIP_DESIGN.md §2):
//   own     a fresh value this scope owns (refcount 1)
//   borrow  a temporary read; not an owner
//   share   a second reference keeps the object alive (ARC behind the scenes)
//   move    ownership leaves this binding (store, return, closure capture, callee)
//   mut     a place is written (field, index, element, module variable)
//   drop    the owner was released
//   keep    not reclaimed here, with the reason (escapes / carried / the callee may
//           keep it / every use can reach another)
//
// Scope: only the file being compiled. Every program carries the stdlib prelude, so
// reporting the whole module would bury five lines of program in two dozen functions of
// strings.nv — which is what the old raw trace did. The frontend hands over the names
// the merge took from other files (ModuleManager::functions_from_other_files); `=all`
// widens the report to everything.
//
// This is a read-only diagnostic: it never inserts, removes or keeps a drop. See
// OWNERSHIP_EXPLAIN_SPEC.md for the format and the bridge table.

#include "backend/nir/NirDiagnostics.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace mlir;

namespace nv {
namespace {

// ── Event model ───────────────────────────────────────────────────────────────

enum class Kind { Own, Share, Borrow, Move, Mut, Drop, Keep };

const char* kind_label(Kind k) {
    switch (k) {
        case Kind::Own:    return "own";
        case Kind::Share:  return "share";
        case Kind::Borrow: return "borrow";
        case Kind::Move:   return "move";
        case Kind::Mut:    return "mut";
        case Kind::Drop:   return "drop";
        case Kind::Keep:   return "keep";
    }
    return "?";
}

// Order of two events on the same line: the value appears, what happens to it, and the
// release last — the order the source reads.
int kind_rank(Kind k) {
    switch (k) {
        case Kind::Own:    return 0;
        case Kind::Mut:    return 1;
        case Kind::Share:  return 2;
        case Kind::Borrow: return 3;
        case Kind::Move:   return 4;
        case Kind::Drop:   return 5;
        case Kind::Keep:   return 6;
    }
    return 7;
}

struct Event {
    Kind kind = Kind::Own;
    unsigned line = 0;
    unsigned col = 0;
    std::string message;
    std::string ir;
};

struct Group {
    std::string label;
    unsigned line = 0;
    std::vector<Event> events;
};

// The callee of a call, whichever dialect the pipeline stopped at (phase A ends with
// func.call; a pass placed later would see llvm.call).
static std::string call_name(Operation* op) {
    if (auto c = dyn_cast<func::CallOp>(op)) return c.getCallee().str();
    if (auto c = dyn_cast<LLVM::CallOp>(op)) {
        if (auto callee = c.getCallee()) return callee->str();
    }
    return "";
}

// ── Bridge knowledge (kept in step with InsertRuntimeDropsPass) ───────────────
// `owns_result` mirrors the drops pass's owned_ret: getting these wrong would explain a
// decision that was never taken. The store list is the destination side, where
// ownership moves without an incref.

static bool is_drop_call(const std::string& c) { return c == "nv_drop"; }

static bool is_global_store(const std::string& c) { return c == "nv_global_set"; }

static bool is_field_store(const std::string& c) { return c == "nv_set_field"; }

static bool is_store_call(const std::string& c) {
    static const char* kStores[] = {
        "nv_set_field", "nv_array_set", "nv_tuple_set", "nv_container_set", "nv_vector_push",
        "nv_global_set",
    };
    for (const char* s : kStores)
        if (c == s) return true;
    if (c.rfind("nv_create_closure", 0) == 0) return true;   // captures are moved in
    return false;
}

static bool owns_result(const std::string& c) {
    if (c.rfind("nv_box_", 0) == 0) return true;
    if (c.rfind("nv_create_", 0) == 0) return true;
    static const char* kFresh[] = {
        "nv_index_to_value", "nv_make_none",
        "nv_add", "nv_sub", "nv_mul", "nv_div", "nv_mod",
        "nv_int_builtin", "nv_float_builtin", "nv_str_builtin",
        "nv_bool_builtin", "nv_char_builtin", "nv_len_builtin",
        "nv_value_lt", "nv_value_gt", "nv_value_le", "nv_value_ge",
        "nv_value_eq", "nv_value_ne",
        "nv_make_some", "nv_make_ok", "nv_make_err",
        "nv_json_field_builtin", "nv_read_builtin",
        "nv_container_get", "nv_array_get", "nv_get_field",
        "nv_thread_spawn", "nv_thread_join",
        "nv_channel_new", "nv_channel_recv",
    };
    for (const char* f : kFresh)
        if (c == f) return true;
    return false;
}

// Accessors whose result is a view into someone else's object: a borrow, not an owner.
static bool returns_view(const std::string& c) {
    static const char* kViews[] = {
        "nv_get_at", "nv_object_get_field", "nv_select", "nv_unwrap_result",
    };
    for (const char* v : kViews)
        if (c == v) return true;
    return false;
}

// Accessors that hand back a value the container still owns, with a reference of their
// own (the pass's owned_ret lists them, hence the drop).
static bool takes_second_reference(const std::string& c) {
    return c == "nv_container_get" || c == "nv_array_get" || c == "nv_get_field";
}

// A compile-time NAME travelling as a boxed string: the variable of a module store, the
// field of a get/set, the method of a dispatch, the symbol of a closure. It is codegen
// plumbing, not a value of the program, so the report never gives it an event of its own
// (`a = 2147483647;` used to show up as "own str — a new value" for the string "a").
static bool is_key_operand(Operation* op, unsigned index) {
    const std::string c = call_name(op);
    if (c == "nv_global_set" || c == "nv_global_get") return index == 0;
    if (c == "nv_set_field") return index == 1;
    if (c == "nv_get_field" || c == "nv_object_get_field")
        return index == op->getNumOperands() - 1;
    if (c.rfind("nv_dispatch_method", 0) == 0) return index == 1;
    if (c.rfind("nv_create_closure", 0) == 0) return index == 0;
    return false;
}

// What the value IS, in source terms. Never the bridge name: whoever reads the report
// is reading a Narval program.
static std::string bridge_noun(const std::string& c) {
    if (c == "nv_box_int" || c == "nv_int_builtin")     return "int";
    if (c == "nv_box_float" || c == "nv_float_builtin") return "float";
    if (c == "nv_box_bool" || c == "nv_bool_builtin")   return "bool";
    if (c == "nv_box_str" || c == "nv_str_builtin")     return "str";
    if (c == "nv_box_char" || c == "nv_char_builtin")   return "char";
    if (c == "nv_len_builtin")                          return "length";
    if (c == "nv_read_builtin")                         return "line read from stdin";
    if (c == "nv_json_field_builtin")                   return "json field";
    if (c == "nv_create_array")                         return "array";
    if (c == "nv_create_vector")                        return "vector";
    if (c == "nv_create_map")                           return "map";
    if (c == "nv_create_tuple")                         return "tuple";
    if (c.rfind("nv_create_closure", 0) == 0)           return "closure";
    if (c.rfind("nv_box_tensor", 0) == 0)               return "tensor";
    if (c.rfind("nv_tensor", 0) == 0)                   return "tensor value";
    if (c == "nv_make_none")                            return "None";
    if (c == "nv_make_some")                            return "Option";
    if (c == "nv_make_ok" || c == "nv_make_err")        return "Result";
    if (c == "nv_thread_spawn")                         return "thread id";
    if (c == "nv_thread_join")                          return "thread result";
    if (c == "nv_channel_new")                          return "channel";
    if (c == "nv_channel_recv")                         return "received message";
    if (c == "nv_container_get" || c == "nv_array_get" || c == "nv_get_at" ||
        c == "nv_get_field" || c == "nv_object_get_field")
        return "element read out of a container";
    if (c == "nv_index_to_value")                       return "indexed value";
    if (c == "nv_select")                               return "value chosen by a conditional";
    if (c == "nv_unwrap_result")                        return "value unwrapped from a Result";
    if (c == "nv_add" || c == "nv_sub" || c == "nv_mul" ||
        c == "nv_div" || c == "nv_mod" || c == "nv_floor_div" || c == "nv_pow")
        return "arithmetic result";
    if (c.rfind("nv_value_", 0) == 0 || c.rfind("nv_band", 0) == 0 ||
        c.rfind("nv_bor", 0) == 0 || c.rfind("nv_bxor", 0) == 0 ||
        c.rfind("nv_shl", 0) == 0 || c.rfind("nv_shr", 0) == 0)
        return "comparison result";
    return "";
}

// The consumer of a read, in source terms (`write(x)` reads x, and the report says
// `write()`, not `nv_write_bridge`).
static std::string consumer_label(const std::string& c) {
    if (c.empty())                    return "an operator";
    if (c == "nv_write_bridge")       return "write()";
    if (c == "nv_len_builtin")        return "len()";
    if (c == "nv_str_builtin")        return "str()";
    if (c == "nv_int_builtin")        return "int()";
    if (c == "nv_float_builtin")      return "float()";
    if (c == "nv_bool_builtin")       return "bool()";
    if (c == "nv_char_builtin")       return "char()";
    if (c == "nv_add" || c == "nv_sub" || c == "nv_mul" || c == "nv_div" ||
        c == "nv_mod" || c == "nv_floor_div" || c == "nv_pow")
        return "an arithmetic operator";
    if (c.rfind("nv_value_", 0) == 0 || c.rfind("nv_band", 0) == 0 ||
        c.rfind("nv_bor", 0) == 0 || c.rfind("nv_bxor", 0) == 0 ||
        c.rfind("nv_shl", 0) == 0 || c.rfind("nv_shr", 0) == 0 ||
        c == "nv_value_is_truthy")
        return "a comparison";
    if (c.rfind("nv_container_get", 0) == 0 || c.rfind("nv_array_get", 0) == 0 ||
        c.rfind("nv_get_at", 0) == 0 || c.rfind("nv_get_field", 0) == 0 ||
        c.rfind("nv_object_get_field", 0) == 0)
        return "an element read";
    if (c.rfind("nv_tensor", 0) == 0) return "a tensor operation";
    // The store side reading its destination: the vector being pushed into, the module
    // store, the object whose field is written.
    if (c == "nv_vector_push" || c == "nv_container_set" || c == "nv_array_set" ||
        c == "nv_tuple_set")
        return "a container write";
    if (c == "nv_global_set") return "the module store";
    if (c == "nv_set_field") return "a field write";
    if (c.rfind("nv_invoke_closure", 0) == 0) return "the closure";
    return "an internal runtime call";   // the name is in --explain-ownership=ir
}

// ── Location helpers ──────────────────────────────────────────────────────────

static unsigned loc_line(Location loc) {
    if (auto f = dyn_cast<FileLineColLoc>(loc)) return f.getLine();
    if (auto n = dyn_cast<NameLoc>(loc)) return loc_line(n.getChildLoc());
    if (auto fused = dyn_cast<FusedLoc>(loc))
        for (Location l : fused.getLocations())
            if (unsigned line = loc_line(l)) return line;
    return 0;
}

static unsigned loc_col(Location loc) {
    if (auto f = dyn_cast<FileLineColLoc>(loc)) return f.getColumn();
    if (auto n = dyn_cast<NameLoc>(loc)) return loc_col(n.getChildLoc());
    if (auto fused = dyn_cast<FusedLoc>(loc))
        for (Location l : fused.getLocations())
            if (unsigned col = loc_col(l)) return col;
    return 0;
}

// The binding name the codegen attached to a value's producer (see
// NIRGenerationContext::name_value): what lets the report say `b` instead of "the value
// on line 7".
static std::string loc_name(Location loc) {
    if (auto n = dyn_cast<NameLoc>(loc)) return n.getName().str();
    if (auto fused = dyn_cast<FusedLoc>(loc))
        for (Location l : fused.getLocations())
            if (auto n = dyn_cast<NameLoc>(l)) return n.getName().str();
    return "";
}

// ── The source file (the events' gutter) ──────────────────────────────────────

class SourceFile {
public:
    explicit SourceFile(const std::string& path) {
        std::ifstream in(path);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines_.push_back(line);
        }
        ok_ = true;
    }
    bool ok() const { return ok_; }
    const std::string& line(unsigned n) const {
        static const std::string empty;
        if (n == 0 || n > lines_.size()) return empty;
        return lines_[n - 1];
    }
private:
    std::vector<std::string> lines_;
    bool ok_ = false;
};

// ── The reporter ──────────────────────────────────────────────────────────────

class OwnershipReporter {
public:
    OwnershipReporter(ModuleOp module, const std::string& source_file,
                      bool all_files, bool ir_detail)
        : module_(module), source_(source_file),
          source_path_(source_file), all_files_(all_files), ir_detail_(ir_detail) {}

    void run() {
        for (Operation& op : module_.getBody()->getOperations()) {
            auto fn = dyn_cast<func::FuncOp>(op);
            if (!fn || fn.empty() || fn.isExternal()) continue;
            if (fn.getName() == "nv_drop") continue;
            if (!in_scope(fn)) {
                ++skipped_;
                if (skipped_names_.size() < 12) skipped_names_.push_back(fn.getName().str());
                continue;
            }
            report_function(fn);
        }
        for (Group& g : groups_)
            std::stable_sort(g.events.begin(), g.events.end(), [](const Event& a, const Event& b) {
                // (line, kind, column): the value first, then what happens to it, then the
                // release — the column only breaks ties inside one kind, so a call's own
                // column (the start of the statement) does not put its reads before the
                // value they read.
                if (a.line != b.line) return a.line < b.line;
                int ra = kind_rank(a.kind), rb = kind_rank(b.kind);
                if (ra != rb) return ra < rb;
                if (a.col != b.col) return a.col < b.col;
                return a.message < b.message;
            });
        // The module's own top level first, then the functions in source order.
        std::stable_sort(groups_.begin(), groups_.end(), [](const Group& a, const Group& b) {
            if (a.line != b.line) {
                if (a.line == 0) return false;
                if (b.line == 0) return true;
                return a.line < b.line;
            }
            return a.label < b.label;
        });
    }

    void print(raw_ostream& os) const {
        os << "[ownership] " << (source_path_.empty() ? std::string("<input>") : source_path_)
           << " — " << owned_ << " value(s) created in this file\n";
        os << "[ownership] own=fresh value · borrow=temporary read · share=a second reference "
              "· move=ownership leaves · mut=place written · drop=released · keep=not "
              "reclaimed\n";
        if (!source_.ok())
            os << "[ownership] (the source file could not be read: events carry line "
                  "numbers only)\n";
        for (const Group& g : groups_) {
            os << "[ownership] " << g.label;
            if (g.line) os << "  (line " << g.line << ")";
            os << "\n";
            unsigned shown_line = 0;
            for (const Event& e : g.events) {
                if (e.line && e.line != shown_line) {
                    shown_line = e.line;
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), "%4u | ", e.line);
                    os << "[ownership] " << buf << source_text(e.line) << "\n";
                }
                char buf[1024];
                std::snprintf(buf, sizeof(buf), "     |   %-7s %s", kind_label(e.kind),
                              e.message.c_str());
                os << "[ownership]" << buf << "\n";
                if (ir_detail_ && !e.ir.empty())
                    os << "[ownership]      |     · " << e.ir << "\n";
            }
        }
        if (skipped_)
            os << "[ownership] " << skipped_ << " function(s) of other files skipped: "
               << joined_names() << " (--explain-ownership=all to include)\n";
        print_summary(os);
    }

private:
    // ── scope ────────────────────────────────────────────────────────────────
    bool in_scope(func::FuncOp fn) const {
        if (all_files_) return true;
        const std::string name = fn.getName().str();
        if (name == "main.start") return true;                    // the program's own top level
        if (name.rfind("__closure_fn", 0) == 0) return true;      // a closure is the file's
        if (diag_prelude_functions().count(name)) return false;
        if (name.rfind("__method_", 0) == 0 || name.rfind("__ctor_", 0) == 0) {
            for (const auto& cls : diag_prelude_classes()) {
                if (name.rfind("__method_" + cls + "_", 0) == 0) return false;
                if (name == "__ctor_" + cls) return false;
            }
            return true;
        }
        return true;
    }

    std::string joined_names() const {
        std::string out;
        for (size_t i = 0; i < skipped_names_.size(); ++i) {
            if (i) out += ", ";
            out += skipped_names_[i];
        }
        if (skipped_ > skipped_names_.size()) out += ", …";
        return out;
    }

    std::string source_text(unsigned line) const {
        std::string text = source_.line(line);
        for (char& c : text)
            if (c == '\t') c = ' ';
        if (text.size() > 60) text = text.substr(0, 57) + "…";
        return text;
    }

    // ── naming ───────────────────────────────────────────────────────────────
    static std::string demangle(const std::string& name) {
        if (name.rfind("__method_", 0) == 0) {
            std::string rest = name.substr(9);
            size_t us = rest.find('_');
            if (us != std::string::npos) return rest.substr(0, us) + "." + rest.substr(us + 1);
        }
        if (name.rfind("__ctor_", 0) == 0) return name.substr(7) + ".new";
        return name;
    }

    static std::string function_label(func::FuncOp fn) {
        std::string name = fn.getName().str();
        if (name == "main.start") return "module scope (top-level code)";
        if (name.rfind("__method_", 0) == 0 || name.rfind("__ctor_", 0) == 0)
            return "method " + demangle(name);
        if (name.rfind("__closure_fn", 0) == 0) return "closure " + name.substr(13);
        return "def " + name;
    }

    // A field/global/variable name reaches the IR as a boxed string:
    // nv_box_str(llvm.mlir.addressof @__narval_str_<text>). Read the global's own value
    // (the symbol suffix alone is mangled for names with escapes).
    std::string string_operand(Value v) {
        if (!v) return "";
        Operation* def = v.getDefiningOp();
        if (!def) return "";
        // The name travels either boxed (`nv_box_str(addressof @__narval_str_x)`, what a
        // module store takes) or as the bare address of the string (what nv_set_field and
        // nv_get_field take): unwrap the box and read the global itself.
        if (call_name(def) == "nv_box_str" && def->getNumOperands() > 0) {
            def = def->getOperand(0).getDefiningOp();
            if (!def) return "";
        }
        auto addr = dyn_cast<LLVM::AddressOfOp>(def);
        if (!addr) return "";
        llvm::StringRef sym = addr.getGlobalName();
        if (auto g = module_.lookupSymbol<LLVM::GlobalOp>(sym)) {
            if (auto str = dyn_cast<StringAttr>(g->getAttr("value"))) {
                std::string text = str.getValue().str();
                if (!text.empty() && text.back() == '\0') text.pop_back();
                return text;
            }
        }
        llvm::StringRef prefix = "__narval_str_";
        if (sym.consume_front(prefix)) return sym.str();
        return "";
    }

    std::string constant_operand(Value v) const {
        if (!v) return "";
        Operation* def = v.getDefiningOp();
        if (!def) return "";
        if (auto c = dyn_cast<arith::ConstantOp>(def)) {
            if (auto i = dyn_cast<IntegerAttr>(c.getValue())) return std::to_string(i.getInt());
            if (auto f = dyn_cast<FloatAttr>(c.getValue()))
                return std::to_string(f.getValueAsDouble());
            return "";
        }
        if (auto c = dyn_cast<LLVM::ConstantOp>(def)) {
            if (auto i = dyn_cast<IntegerAttr>(c.getValue())) return std::to_string(i.getInt());
        }
        return "";
    }

    // How the report names a value: the binding the codegen attached, else the literal it
    // was built from, else what it is ("int", "arithmetic result", ...).
    std::string value_text(Value v, func::FuncOp fn) {
        if (!v) return "value";
        if (Operation* def = v.getDefiningOp()) {
            if (std::string named = loc_name(def->getLoc()); !named.empty())
                return "\"" + named + "\"";
            std::string c = call_name(def);
            if (!c.empty()) {
                if (c.rfind("nv_dispatch_method", 0) == 0 && def->getNumOperands() > 1) {
                    std::string m = string_operand(def->getOperand(1));
                    return m.empty() ? "the method's result"
                                     : "the result of the method \"" + m + "()\"";
                }
                if (c.rfind("nv_", 0) != 0) return "the result of " + demangle(c) + "()";
                // A read that names its source: a module variable, a field, an element.
                if (c == "nv_global_get" && def->getNumOperands())
                    return "variable \"" + string_operand(def->getOperand(0)) + "\"";
                if ((c == "nv_get_field" || c == "nv_object_get_field") &&
                    def->getNumOperands() > 1)
                    return "field \"" + string_operand(def->getOperand(def->getNumOperands() - 1)) +
                           "\" of " + value_text(def->getOperand(0), fn);
                if ((c == "nv_container_get" || c == "nv_array_get" || c == "nv_get_at") &&
                    def->getNumOperands())
                    return "an element of " + value_text(def->getOperand(0), fn);
                std::string noun = bridge_noun(c);
                if (noun.empty()) return "value";
                std::string lit = def->getNumOperands() ? constant_operand(def->getOperand(0)) : "";
                if (noun == "str" && def->getNumOperands()) {
                    std::string s = string_operand(def->getOperand(0));
                    if (!s.empty()) return "str \"" + s + "\"";
                }
                return lit.empty() ? noun : noun + " " + lit;
            }
            return "value";
        }
        if (auto arg = dyn_cast<BlockArgument>(v)) {
            if (arg.getOwner() == &fn.front() && arg.getArgNumber() == 0 &&
                fn.getName() != "main.start")
                return "\"self\"";
            return "the value carried into this block";
        }
        return "value";
    }

    // The `new` sequence stamps the object with its class name (a field the runtime reads
    // back to dispatch): the report is about the fields the program declared, so this
    // store and the name it carries are not events.
    bool is_internal_store(Operation* op) {
        if (call_name(op) != "nv_set_field" || op->getNumOperands() < 2) return false;
        return string_operand(op->getOperand(1)) == "__class_name__";
    }

    std::string place_of(Operation* store, func::FuncOp fn) {
        const std::string c = call_name(store);
        if (is_global_store(c))
            return "variable \"" + string_operand(store->getOperand(0)) + "\" in the module store";
        if (is_field_store(c)) {
            std::string field = string_operand(store->getOperand(1));
            std::string owner = "the object";
            if (auto arg = dyn_cast<BlockArgument>(store->getOperand(0)))
                if (arg.getOwner() == &fn.front() && arg.getArgNumber() == 0 &&
                    fn.getName() != "main.start")
                    owner = "self";
            return "field \"" + field + "\" of " + owner;
        }
        if (c == "nv_vector_push")
            return "an element appended to " + value_text(store->getOperand(0), fn);
        if (c.rfind("nv_create_closure", 0) == 0)
            return "the closure's captured values";
        return "an element of " + value_text(store->getOperand(0), fn);
    }

    // ── the walk ─────────────────────────────────────────────────────────────
    struct Fate {
        std::vector<Operation*> drops;
        std::vector<Operation*> stores;
        std::vector<Operation*> returns;
        std::vector<Operation*> branches;
        std::vector<Operation*> handed_to_callee;
        std::vector<Operation*> reads;
    };
    struct ValueInfo {
        Operation* producer = nullptr;
        bool owned = false;
        bool view = false;
        bool user_call = false;
        bool block_arg = false;
    };

    void report_function(func::FuncOp fn) {
        Group group;
        group.label = function_label(fn);

        llvm::SmallVector<Operation*, 64> ops;
        fn.walk([&](Operation* op) { ops.push_back(op); });

        for (Operation* op : ops) {
            unsigned line = loc_line(op->getLoc());
            if (line && (!group.line || line < group.line)) group.line = line;
        }

        llvm::DenseMap<Value, ValueInfo> info;
        llvm::DenseMap<Value, Fate> fates;
        // Values that are only ever a NAME handed to a store/get (the boxed string of a
        // module variable or a field): the report is about the program's values, and
        // `a = 1;` used to show the string "a" being created and dropped.
        llvm::DenseSet<Value> key_values;

        for (Operation* op : ops) {
            const std::string c = call_name(op);
            if (c.empty() || is_drop_call(c) || is_store_call(c)) continue;
            if (op->getNumResults() == 0) continue;
            Value res = op->getResult(0);
            if (!isa<LLVM::LLVMPointerType>(res.getType())) continue;
            ValueInfo& vi = info[res];
            vi.producer = op;
            vi.owned = owns_result(c);
            vi.view = returns_view(c);
            // A function of the program, or a method dispatched by name through an
            // interface: the caller has no ownership annotation for either, so the result
            // is never reclaimed here.
            vi.user_call = c.rfind("nv_", 0) != 0 || c.rfind("nv_dispatch_method", 0) == 0;
        }
        for (Block& blk : fn.getBlocks()) {
            if (&blk == &fn.front()) continue;
            for (Value arg : blk.getArguments())
                if (isa<LLVM::LLVMPointerType>(arg.getType())) info[arg].block_arg = true;
        }

        for (Operation* op : ops) {
            const std::string c = call_name(op);
            const bool is_op_drop    = is_drop_call(c);
            const bool is_op_internal = is_internal_store(op);
            const bool is_op_store   = !is_op_internal && is_store_call(c);
            const bool is_op_return  = isa<func::ReturnOp>(op) || isa<LLVM::ReturnOp>(op);
            const bool is_op_branch  = op->hasTrait<OpTrait::IsTerminator>() && !is_op_return;
            const bool is_op_method  = c.rfind("nv_dispatch_method", 0) == 0;
            const bool is_op_user    = !c.empty() && !is_op_drop && !is_op_store &&
                                       !is_op_method && c.rfind("nv_", 0) != 0;

            for (auto it : llvm::enumerate(op->getOperands())) {
                Value v = it.value();
                if (!isa<LLVM::LLVMPointerType>(v.getType())) continue;
                if (is_op_internal) continue;                       // a `new`'s class stamp
                if (is_key_operand(op, it.index())) {                // a name, not a value
                    key_values.insert(v);
                    continue;
                }
                Fate& f = fates[v];
                if (is_op_drop) { f.drops.push_back(op); continue; }
                if (is_op_store && it.index() == op->getNumOperands() - 1) {
                    f.stores.push_back(op);
                    continue;
                }
                if (is_op_return) { f.returns.push_back(op); continue; }
                if (is_op_branch) { f.branches.push_back(op); continue; }
                if (is_op_user || is_op_method) { f.handed_to_callee.push_back(op); continue; }
                f.reads.push_back(op);
            }
        }

        // One event stream per value. The counters count VALUES for the lifecycle
        // events (a value released on two exclusive paths is still one released value)
        // and events for the per-use ones (a borrow, a share, a place written).
        for (auto& entry : fates) {
            Value v = entry.first;
            Fate& f = entry.second;
            if (key_values.count(v)) continue;   // a compile-time name, not a program value
            ValueInfo vi = info.count(v) ? info[v] : ValueInfo{};
            if (!vi.owned && !vi.view && !vi.user_call && !vi.block_arg) continue;

            const std::string text = value_text(v, fn);
            const bool had_other_fate = !f.stores.empty() || !f.returns.empty() ||
                                        !f.handed_to_callee.empty();
            bool kept_this_value = false;

            if (vi.owned && !takes_second_reference(call_name(v.getDefiningOp()))) {
                emit(group, Kind::Own, loc_for(v.getDefiningOp(), v), text + " — a new value",
                     v.getDefiningOp(), 0);
                ++owned_;
            }
            if (vi.view) {
                emit(group, Kind::Borrow, loc_for(v.getDefiningOp(), v),
                     text + " — a view into an object someone else owns (nothing to release)",
                     v.getDefiningOp(), 0);
                ++borrowed_;
            }
            if (vi.owned && takes_second_reference(call_name(v.getDefiningOp()))) {
                // The accessor hands back a value the container keeps, with a reference of
                // its own: not a new value, a second reference to an existing one.
                emit(group, Kind::Share, loc_for(v.getDefiningOp(), v),
                     text + " — the read takes a second reference of it (the reader owns "
                            "that reference)", v.getDefiningOp(), 0);
                ++shared_;
            }
            if (vi.user_call && f.drops.empty() && !had_other_fate && f.branches.empty()) {
                emit(group, Kind::Keep, loc_for(v.getDefiningOp(), v),
                     text + " — not reclaimed: a call carries no ownership annotation "
                            "across its boundary",
                     v.getDefiningOp(), 0);
                kept_this_value = true;
            }

            for (Operation* d : f.drops) {
                std::string reason;
                if (f.reads.empty() && !had_other_fate && f.branches.empty())
                    reason = "released at its definition: it is never read";
                else if (f.drops.size() > 1)
                    reason = "released here: the last use on this path";
                else
                    reason = "released after its last use";
                emit(group, Kind::Drop, loc_for(d, v), text + " — " + reason, d, f.reads.size());
            }
            if (!f.drops.empty()) ++released_;

            for (Operation* r : f.returns) {
                emit(group, Kind::Move, loc_for(r, v), text + " — returned to the caller", r, 0);
            }
            for (Operation* s : f.stores) {
                emit(group, Kind::Move, loc_for(s, v),
                     text + " — stored into " + place_of(s, fn) + " (the destination owns it)",
                     s, 0);
                emit(group, Kind::Mut, loc_for(s, v), place_of(s, fn) + " written", s, 0);
            }
            for (Operation* r : f.handed_to_callee) {
                const std::string c = call_name(r);
                std::string what;
                if (c.rfind("nv_dispatch_method", 0) == 0 && r->getNumOperands() > 1)
                    what = "the method \"" + string_operand(r->getOperand(1)) + "\"";
                else if (c.rfind("nv_create_closure", 0) == 0)
                    what = "the closure";
                else
                    what = "\"" + demangle(c) + "()\"";
                emit(group, Kind::Move, loc_for(r, v),
                     text + " — handed to " + what + " (the callee may keep it)", r, 0);
            }
            if (!f.branches.empty()) {
                emit(group, Kind::Keep, f.branches.front()->getLoc(),
                     text + " — carried by a branch: the destination block owns it",
                     f.branches.front(), 0);
                kept_this_value = true;
            }
            for (Operation* r : f.reads) {
                const std::string c = call_name(r);
                if (c == "nv_incref_bridge") {
                    emit(group, Kind::Share, loc_for(r, v),
                         text + " — a second reference is taken (a container keeps it)", r, 0);
                    ++shared_;
                    continue;
                }
                emit(group, Kind::Borrow, loc_for(r, v),
                     text + " — read by " + consumer_label(c), r, 0);
                ++borrowed_;
            }
            if (vi.owned && f.drops.empty() && !had_other_fate && f.branches.empty() &&
                !f.reads.empty()) {
                emit(group, Kind::Keep, loc_for(v.getDefiningOp(), v),
                     text + " — not reclaimed: every use can reach another (it is still live "
                            "on that path)",
                     v.getDefiningOp(), f.reads.size());
                kept_this_value = true;
            }
            if (vi.block_arg && !f.reads.empty() && f.drops.empty() && f.branches.empty()) {
                // A value carried into a block (a loop's index or accumulator) and read
                // again on the next iteration: no use is the last one.
                emit(group, Kind::Keep, fn.getLoc(),
                     text + " — not reclaimed: it is live across the loop",
                     v.getDefiningOp(), 0);
                kept_this_value = true;
            }
            if (!f.returns.empty() || !f.stores.empty() || !f.handed_to_callee.empty())
                ++moved_;
            if (kept_this_value) ++kept_;
        }

        if (!group.events.empty()) groups_.push_back(std::move(group));
    }

    // Where an event happened: the op's own location when it has one (the codegen
    // attaches them to the ops the statements build), else the line of the value the
    // event is about — several lowerings drop the location of the op they create, and
    // without this the report would print those events with no source line at all.
    Location loc_for(Operation* op, Value v) {
        if (op && loc_line(op->getLoc())) return op->getLoc();
        if (v) {
            if (Operation* def = v.getDefiningOp())
                if (loc_line(def->getLoc())) return def->getLoc();
            // The producer's own location was dropped by a lowering (a `new`'s sequence is
            // the usual one): a use is the statement that consumed the value, and that one
            // carries a location. The earliest of them is the one that reads as "where this
            // value came from".
            unsigned best = 0;
            Location best_loc = UnknownLoc::get(module_.getContext());
            for (Operation* user : v.getUsers()) {
                unsigned line = loc_line(user->getLoc());
                if (!line) continue;
                if (!best || line < best) { best = line; best_loc = user->getLoc(); }
            }
            if (best) return best_loc;
        }
        return op ? op->getLoc() : UnknownLoc::get(module_.getContext());
    }

    void emit(Group& g, Kind kind, Location loc, const std::string& message,
              Operation* op, unsigned uses) {
        Event e;
        e.kind = kind;
        e.line = loc_line(loc);
        e.col = loc_col(loc);
        e.message = message;
        if (ir_detail_) {
            std::string detail = op ? op_ir(op) : "";
            if (uses) detail += (detail.empty() ? "" : "; ") + std::to_string(uses) + " use(s)";
            e.ir = detail;
        }
        g.events.push_back(std::move(e));
    }

    std::string op_ir(Operation* op) const {
        std::string c = call_name(op);
        if (!c.empty()) return "called @" + c;
        return op->getName().getStringRef().str();
    }

    void print_summary(raw_ostream& os) const {
        unsigned written = 0;
        for (const Group& g : groups_)
            for (const Event& e : g.events)
                if (e.kind == Kind::Mut) ++written;
        os << "[ownership] summary: " << owned_ << " own · " << released_ << " drop · "
           << borrowed_ << " borrow · " << moved_ << " move · " << shared_ << " share · "
           << written << " mut · " << kept_ << " keep\n";
        if (!kept_) return;
        unsigned shown = 0;
        for (const Group& g : groups_) {
            for (const Event& e : g.events) {
                if (e.kind != Kind::Keep) continue;
                if (shown == 6) { os << "[ownership]   …\n"; return; }
                os << "[ownership]   kept at line " << e.line << ": " << e.message << "\n";
                ++shown;
            }
        }
    }

    ModuleOp module_;
    SourceFile source_;
    std::string source_path_;
    bool all_files_;
    bool ir_detail_;
    std::vector<Group> groups_;
    size_t skipped_ = 0;
    std::vector<std::string> skipped_names_;
    unsigned owned_ = 0, released_ = 0, moved_ = 0, borrowed_ = 0, shared_ = 0, kept_ = 0;
};

// ── The pass ──────────────────────────────────────────────────────────────────

struct ExplainOwnershipPass
    : public PassWrapper<ExplainOwnershipPass, OperationPass<ModuleOp>> {
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ExplainOwnershipPass)

    ExplainOwnershipPass(std::string source_file, bool all_files, bool ir_detail)
        : source_file_(std::move(source_file)), all_files_(all_files), ir_detail_(ir_detail) {}

    StringRef getArgument() const final { return "narval-explain-ownership"; }
    StringRef getDescription() const final {
        return "Report the ownership decisions of the compiled source file "
               "(own/borrow/share/move/mut/drop/keep)";
    }

    void runOnOperation() override {
        OwnershipReporter reporter(getOperation(), source_file_, all_files_, ir_detail_);
        reporter.run();
        reporter.print(llvm::errs());
    }

    std::string source_file_;
    bool all_files_;
    bool ir_detail_;
};

} // namespace

std::unique_ptr<mlir::Pass> createExplainOwnershipPass(const std::string& source_file,
                                                       bool all_files,
                                                       bool ir_detail) {
    return std::make_unique<ExplainOwnershipPass>(source_file, all_files, ir_detail);
}

} // namespace nv
