set -e

OUT=cleaner
SRCS=(main.cpp daemon.cpp pidfile.cpp)

echo "Building ${OUT} ..."

rm -f "${OUT}" *.o

g++ -std=c++17 -Wall -Werror -O2 "${SRCS[@]}" -o "${OUT}"

echo "Build finished: ./${OUT}"
