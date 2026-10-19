#!/usr/bin/env fish
# Compila localmente sem instalar o Tarefas11 no sistema.
set -l project (path dirname (status filename))

# Arquivos de um ZIP podem ter datas futuras quando criado em UTC e
# extraido no fuso America/Sao_Paulo. Isso causa o loop do Ninja/CMake.
# Normaliza SOMENTE os arquivos de fonte com data mais de 2 min no futuro.
set -l future_cutoff (date -d '+2 minutes' '+%Y-%m-%d %H:%M:%S')
set -l future_path (find "$project" -path "$project/build" -prune -o -type f -newermt "$future_cutoff" -print -quit)
if test -n "$future_path"
    printf '%s\n' '[Tarefas11] Detectei fontes com horario futuro; corrigindo datas para impedir loop do Ninja.'
    find "$project" -path "$project/build" -prune -o -type f -newermt "$future_cutoff" -exec touch '{}' +
    or exit $status
end

cmake -S "$project" -B "$project/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
or exit $status
cmake --build "$project/build" --parallel (nproc)
or exit $status
printf '\nBuild finalizado. Execute: %s/build/tarefas11\n' "$project"
