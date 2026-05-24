#include "backend/nir/NarvalDialect.h"
#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalTypes.h"
#include "backend/nir/NarvalAttrs.h"

#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

// Pull in the generated dialect implementation (ctor, dtor, TypeID).
#include "NarvalDialect.cpp.inc"

using namespace mlir;
using namespace mlir::narval;

void NarvalDialect::initialize() {
    addOperations<
#define GET_OP_LIST
#include "NarvalOps.cpp.inc"
    >();

    addTypes<
#define GET_TYPEDEF_LIST
#include "NarvalTypes.cpp.inc"
    >();

    addAttributes<
#define GET_ATTRDEF_LIST
#include "NarvalAttrs.cpp.inc"
    >();
}

// Attribute printer / parser — dispatch to generated narval.* attr parsers.
mlir::Attribute NarvalDialect::parseAttribute(mlir::DialectAsmParser& parser,
                                               mlir::Type type) const {
    llvm::StringRef tag;
    if (parser.parseKeyword(&tag).failed())
        return {};
    mlir::Attribute attr;
    auto result = generatedAttrParser(parser, tag, type, attr);
    if (result.has_value())
        return attr;
    parser.emitError(parser.getNameLoc(),
                     "unknown narval attribute: ") << tag;
    return {};
}

void NarvalDialect::printAttribute(mlir::Attribute attr,
                                    mlir::DialectAsmPrinter& printer) const {
    if (generatedAttrPrinter(attr, printer).succeeded())
        return;
    printer << "<unknown narval attribute>";
}

// parseType / printType implementations generated from NarvalTypes.td.
#define GET_TYPEDEF_CLASSES
#include "NarvalTypes.cpp.inc"

// Attr printer / parser implementations generated from NarvalAttrs.td.
#define GET_ATTRDEF_CLASSES
#include "NarvalAttrs.cpp.inc"
