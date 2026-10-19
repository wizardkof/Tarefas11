#!/usr/bin/env fish
# Executa uma verificação de inicialização QML em modo headless após compilar.
set -l project (path dirname (status filename))
set -l binary "$project/build/tarefas11"
if not test -x "$binary"
    echo "ERRO: compile antes com: fish build.fish"
    exit 1
end
set -l output (mktemp)
env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software timeout 5 "$binary" > "$output" 2>&1
set -l code $status
if grep -E 'QQmlApplicationEngine failed|ReferenceError:|TypeError: Cannot read|Cannot assign to non-existent property|module .* is not installed|Type .* unavailable' "$output"
    printf '\nFALHA: erro crítico durante carga de QML. Log: %s\n' "$output"
    exit 2
end
if test $code -eq 124
    echo "PASS: QML carregou e o processo permaneceu vivo por 5 segundos (offscreen)."
    rm -f "$output"
    exit 0
end
if test $code -eq 0
    printf 'ATENÇÃO: processo terminou cedo sem erro registrado; consulte: %s\n' "$output"
    exit 3
end
printf 'FALHA: processo terminou com código %s. Log: %s\n' $code "$output"
cat "$output"
exit $code
