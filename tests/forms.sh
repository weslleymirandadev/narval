#!/bin/bash
# Cobertura por FORMA sintatica.
#
# Os casos de teste exercitam PROGRAMAS. Uma forma sintatica que nenhum caso usa pode estar
# quebrada sem ninguem ver, e isso aconteceu duas vezes: `and`/`or` eram sintaxe morta em toda a
# linguagem (nenhum dos 168 casos usava), e um acumulador condicional dentro de um `for` devolvia o
# valor de antes do laco, sem erro nenhum. Este script cruza a lista de formas do parser contra o
# texto dos casos e diz quais nao sao exercitadas por ninguem.
#
# Uso:
#   bash tests/forms.sh            tabela de cobertura + as formas sem nenhum caso
#   bash tests/forms.sh --strict   falha (rc=1) se alguma forma estiver sem cobertura
#
# LIMITES, para nao confiar demais no resultado:
#   - a marca e' heuristica: procura a forma no TEXTO do caso, com os comentarios (`#`) removidos.
#     Forma reconhecida por acidente (uma string que parece um `if`) conta como cobertura. O `//`
#     NAO e' removido de proposito: em Narval ele e' o operador de divisao inteira, nao comentario;
#   - so' vale para formas reconheciveis numa linha. Uma que precise de contexto multi-linha (o
#     `for ... else`, por exemplo) nao e' detectavel por grep; essas tem caso proprio e a lista nao
#     as cobre (ver tests/cases/syntax_for_else.nv);
#   - a lista e' a parte que envelhece, nos DOIS sentidos: forma nova no parser entra aqui (sem
#     isso o instrumento nao a ve), e forma REMOVIDA da linguagem sai daqui (o `&&` saiu quando
#     `and` virou o operador). Uma forma que nao existe mais como item apareceria para sempre na
#     lista de "sem cobertura", que e' ruido;
#   - so' olha tests/cases/*.nv — o contrato da suite. Os PoCs (tests/poc_*.nv) exercitam mais
#     coisa, mas nao sao verificados por ninguem, entao contar com eles inflaria a cobertura;
#   - algumas formas estao SEM cobertura de proposito, porque estao quebradas e um caso so' pode ser
#     escrito depois do conserto (ver REMAINING_WORK_SPEC.md, item 2): `defer error`, generic ctor
#     sem `new`, `abstract class`, `instanceof`. Com --strict o script falha; ele fica verde quando
#     essas quatro forem consertadas.

set -u

CASES_DIR="$(cd "$(dirname "$0")" && pwd)/cases"
STRICT=0
[ "${1:-}" = "--strict" ] && STRICT=1

if [ ! -d "$CASES_DIR" ]; then
    echo "sem $CASES_DIR" >&2
    exit 2
fi

# nome|regex estendida. Cuidado com o formato: o corte e' no PRIMEIRO '|', entao o NOME nao pode
# conter '|' (um nome com pipe desloca o corte e deixa a regex comecando por '|', que em ERE e'
# ramo vazio e casa com qualquer coisa — foi assim que "closure |x|" marcou 176 de 176 casos).
# A regex, depois do primeiro '|', pode conter '|' a vontade. Entradas com aspas: apostrofos.
FORMAS=(
    'declaracao mut|(^|[^A-Za-z_])mut +[A-Za-z_][A-Za-z0-9_]* *[:=]'
    'atribuicao|^ *[A-Za-z_][A-Za-z0-9_]* *= *[^=]'
    'def|(^|[^A-Za-z_])def +[A-Za-z_]'
    'async def|(^|[^A-Za-z_])async +def'
    'class|(^|[^A-Za-z_])class +[A-Za-z_]'
    'abstract class|(^|[^A-Za-z_])abstract +class'
    'interface|(^|[^A-Za-z_])interface +[A-Za-z_]'
    'enum|(^|[^A-Za-z_])enum +[A-Za-z_]'
    'metodo sem def|(^|[^A-Za-z_])(public|private|protected) +[A-Za-z_][A-Za-z0-9_]* *\(|^ *[A-Za-z_][A-Za-z0-9_]* *\([^)]*\) *: *[A-Za-z_]+ *\{'
    'construtor new\(\)|(^|[^A-Za-z_])new *\('
    'if|(^|[^A-Za-z_])if[ (]'
    'elif|(^|[^A-Za-z_])elif'
    'else|(^|[^A-Za-z_])else'
    'if inline (if c: stmt;)|if [^{]*: *[A-Za-z_]'
    'while|(^|[^A-Za-z_])while[ (]'
    'for in|(^|[^A-Za-z_])for +[A-Za-z_][A-Za-z0-9_]* +in '
    'forever|(^|[^A-Za-z_])forever'
    'range 0..n|[0-9] *\.\. *[A-Za-z0-9]'
    'range 1..=n|\.\.='
    'match|(^|[^A-Za-z_])match[ (]'
    'return|(^|[^A-Za-z_])return[ ;]'
    'return vazio|return *;'
    'break|(^|[^A-Za-z_])break *;'
    'continue|(^|[^A-Za-z_])continue *;'
    'try|(^|[^A-Za-z_])try[ {]'
    'catch|(^|[^A-Za-z_])catch'
    'finally|(^|[^A-Za-z_])finally'
    'throw|(^|[^A-Za-z_])throw '
    'defer|(^|[^A-Za-z_])defer *\{'
    'defer error|defer +error'
    'propagate|(^|[^A-Za-z_])propagate'
    'Ok()/Err()|(^|[^A-Za-z_.])(Ok|Err) *\('
    'import from|(^|[^A-Za-z_])from +"'
    'import bare|^ *import +[A-Za-z_"]'
    'extern|(^|[^A-Za-z_])extern'
    'comptime|(^|[^A-Za-z_])comptime'
    'inline condicional|(^|[^A-Za-z_])inline '
    'asm|(^|[^A-Za-z_])asm *\{'
    'naked_asm|naked_asm'
    'atributo @[..]|@\['
    'decorador @nome(|@[A-Za-z_][A-Za-z0-9_]* *\('
    'chamada de macro nome!{|[A-Za-z_][A-Za-z0-9_]*! *\{'
    'new Classe|(^|[^A-Za-z_])new +[A-Za-z_]'
    'generics <T>|<[A-Za-z_][A-Za-z0-9_]*>'
    'generic ctor sem new|[A-Za-z_][A-Za-z0-9_]*<[A-Za-z_][A-Za-z0-9_]*> *\('
    'closure de um parametro|[|][A-Za-z_ ,:]*[|] *[a-zA-Z{]'
    'closure vazia|[|] +[|]'
    'tipo vetor|[^A-Za-z0-9_:] *\[[A-Za-z_][A-Za-z0-9_]*\]'
    'tipo array [N]|: *[A-Za-z_][A-Za-z0-9_]*\[[0-9]+\]'
    'tipo map<K,V>|(^|[^A-Za-z_])map<'
    'tipo map<K,V> (M maiusculo)|(^|[^A-Za-z_])Map<'
    'await|(^|[^A-Za-z_])await '
    'instanceof|(^|[^A-Za-z_])instanceof '
    'spawn|(^|[^A-Za-z_])spawn *\('
    'cast/conversao as| as +[A-Za-z_]'
    'expressao condicional (a if c else b)|[A-Za-z0-9_)] +if +[^;]+ +else +'
    'operador and|(^|[^A-Za-z_])and[ (]'
    'operador or|(^|[^A-Za-z_])or[ (]'
    'operador shift|>>|<<'
    'operador potencia|\*\*'
    'operador divisao inteira|//'
    'operador bitwise| [&^|] '
    'operador modulo| % '
    'atribuicao composta|\+='
)

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Copia os casos sem os comentarios de `#`, para nao contar uma forma citada em prosa.
n_cases=0
for f in "$CASES_DIR"/*.nv; do
    [ -e "$f" ] || continue
    sed -e 's/#.*//' "$f" > "$tmp/$(basename "$f")"
    n_cases=$((n_cases + 1))
done

printf 'cobertura por forma sintatica — %d casos\n\n' "$n_cases"
printf '%-46s %6s  %s\n' "FORMA" "CASOS" "EXEMPLOS"
printf '%s\n' "--------------------------------------------------------------------------------------------"

sem_cobertura=()
for entry in "${FORMAS[@]}"; do
    nome="${entry%%|*}"
    regex="${entry#*|}"
    arquivos=$(grep -El "$regex" "$tmp"/*.nv 2>/dev/null || true)
    if [ -z "$arquivos" ]; then
        n=0
    else
        n=$(printf '%s\n' "$arquivos" | grep -c .)
    fi
    exemplos=$(printf '%s\n' "$arquivos" | head -2 | xargs -r -n1 basename | tr '\n' ' ')
    printf '%-46s %6d  %s\n' "$nome" "$n" "$exemplos"
    [ "$n" -eq 0 ] && sem_cobertura+=("$nome")
done

printf '\n'
if [ ${#sem_cobertura[@]} -eq 0 ]; then
    echo "toda forma da lista e' exercitada por algum caso."
    exit 0
fi

echo "SEM NENHUM CASO (${#sem_cobertura[@]} de ${#FORMAS[@]}):"
for nome in "${sem_cobertura[@]}"; do echo "  - $nome"; done

if [ "$STRICT" -eq 1 ]; then
    echo
    echo "--strict: falhando por causa das formas acima."
    exit 1
fi
exit 0
