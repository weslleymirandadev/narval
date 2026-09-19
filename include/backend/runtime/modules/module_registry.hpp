#pragma once
// Registro dos módulos de runtime do Narval.
//
// Um módulo é uma capacidade (crypto, net, ...) que o compilador expõe com nome qualificado —
// `crypto.sha512(...)` — e só quando o programa o importa. O símbolo C por trás é sempre
// nv_<modulo>_<nome>_builtin, e o usuário nunca o escreve: encapsular não é esconder o nome,
// é parar de usá-lo como API.
//
// Fonte da verdade: o `.def` do módulo (uma linha por função). Este header é header-only de
// propósito — checker e codegen o incluem sem uma biblioteca nova no meio. As declarações C
// saem de `crypto_decls.h`, que inclui o mesmo `.def` no modo declaração.
//
// Plano completo: RUNTIME_MODULES_DESIGN.md.
#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace nv {

struct RuntimeFn {
    std::string name;        // "sha512"
    std::string params;      // "bytes, bytes" — tokens do .def (bytes | num)
    std::string ret;         // "bytes" | "num" | "flag"
    std::string c_symbol;    // "nv_crypto_sha512_builtin"
    int         arity = 0;

    // `bytes` é string; `num` é inteiro; `big` é inteiro de 64 bits na ABI C (long long);
    // `flag` é inteiro que o checker lê como booleano de verdade. Para o checker, tudo que
    // não é `bytes` conta como inteiro.
    bool returns_int() const { return ret != "bytes"; }

    // Token "num" (inteiro) vs "bytes" (hex): o checker precisa disso para tipar os
    // parâmetros, e o codegen para nada (a ABI é toda valor).
    std::vector<bool> param_is_int() const {
        std::vector<bool> out;
        // `params` chega stringificado do .def, ou seja COM os parênteses: "(bytes, num)"
        // ou "()" para zero parâmetros. Tirar os parênteses antes de dividir é o que faz
        // `()` virar zero parâmetros em vez de um parâmetro chamado "()".
        std::string clean;
        for (char c : params) if (c != '(' && c != ')') clean.push_back(c);
        const std::string& src = clean;
        size_t start = 0;
        while (start < src.size()) {
            size_t comma = src.find(',', start);
            std::string tok = src.substr(start, comma == std::string::npos ? std::string::npos
                                                                           : comma - start);
            while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
            while (!tok.empty() && tok.back() == ' ') tok.pop_back();
            if (!tok.empty()) out.push_back(tok != "bytes");   // num | big
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return out;
    }
};

struct RuntimeModule {
    std::string              name;
    std::vector<RuntimeFn>   fns;

    const RuntimeFn* find(std::string_view fn) const {
        for (const auto& f : fns) if (f.name == fn) return &f;
        return nullptr;
    }
};

// As tabelas. Uma linha por módulo; as funções de cada um vêm do `.def` dele, então não há
// como a superfície da linguagem divergir do que o runtime implementa.
inline const std::vector<RuntimeModule>& runtime_modules() {
    static const std::vector<RuntimeModule> mods = [] {
        std::vector<RuntimeModule> out;
        {
            // crypto — Ed25519 (identidade/assinatura), X25519 (ECDHE),
            // XSalsa20-Poly1305 (secretbox), SHA-512, HMAC-SHA512 e Poly1305.
            // Implementação: src/backend/runtime/modules/crypto/.
#define NV_FN(name, params, ret, arity) { #name, #params, #ret, "nv_crypto_" #name "_builtin", arity },
            const RuntimeFn fns[] = {
#include "crypto.def"
            };
#undef NV_FN
            out.push_back({ "crypto", std::vector<RuntimeFn>(std::begin(fns), std::end(fns)) });
        }
        {
            // system — o ambiente do processo: variáveis e diretório do usuário. As strings
            // aqui são TEXTO (não hex): o token `bytes` do .def diz "string do Narval" ao
            // checker, e o que cada módulo carrega na string é dito no .def dele.
#define NV_FN(name, params, ret, arity) { #name, #params, #ret, "nv_system_" #name "_builtin", arity },
            const RuntimeFn fns[] = {
#include "system.def"
            };
#undef NV_FN
            out.push_back({ "system", std::vector<RuntimeFn>(std::begin(fns), std::end(fns)) });
        }
        {
            // bait — as primitivas do stack BAIT que são exatas e ficam no runtime: mac1
            // (filtro de pré-autenticação do ICSP/BITE). O resto do stack é Narval, em
            // stdlib/bait.nv.
#define NV_FN(name, params, ret, arity) { #name, #params, #ret, "nv_bait_" #name "_builtin", arity },
            const RuntimeFn fns[] = {
#include "bait.def"
            };
#undef NV_FN
            out.push_back({ "bait", std::vector<RuntimeFn>(std::begin(fns), std::end(fns)) });
        }
        return out;
    }();
    return mods;
}

inline const RuntimeModule* find_runtime_module(std::string_view name) {
    for (const auto& m : runtime_modules()) if (m.name == name) return &m;
    return nullptr;
}

// Nome do módulo a partir do caminho de um import:
// "crypto" | "stdlib/crypto.nv" | "../stdlib/crypto.nv" -> "crypto".
inline std::string runtime_module_name_of_import(const std::string& module_path) {
    std::string p = module_path;
    while (!p.empty() && (p.front() == '"' || p.front() == '\'')) p.erase(p.begin());
    while (!p.empty() && (p.back() == '"' || p.back() == '\'')) p.pop_back();
    size_t slash = p.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    return base;
}

}  // namespace nv
