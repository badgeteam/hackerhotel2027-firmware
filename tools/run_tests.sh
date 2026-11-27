#!/usr/bin/env bash
# Runs every test at once: tools/isp_test/isp_test.js, tools/emu/rv32_test
# and each tools/*/run.sh, each into its own log. Then it prints the logs and
# a table of the results. It exits 1 if a test failed. CI runs this.
#
#   tools/run_tests.sh              every test, 2 per CPU at a time
#   tools/run_tests.sh -j 1         one test at a time
#   tools/run_tests.sh four ir      only the tests whose names start so
#
# The emulator and the apps are built once first, so that the tests' own
# make calls find them up to date and do not build them at the same time.
# Most tests wait for the emulator in real time, so more tests than CPUs
# can run at once. PREFIX is the RISC-V toolchain as in the Makefiles
# (riscv64-elf by default; CI: riscv64-unknown-elf).
#
# A test that runs longer than HH27_TEST_TIMEOUT seconds (300 by default)
# is stopped and fails. Ctrl-C, or a cancelled CI job, stops every test and
# prints the end of the log of each test that was still running.
# It needs bash 5 (EPOCHREALTIME, wait -n) and, for the time limit, GNU
# timeout (coreutils).
set -u

if [ "${BASH_VERSINFO[0]}" -lt 5 ]; then
    echo "$0: needs bash 5 or newer, this is $BASH_VERSION" >&2
    exit 2
fi

usage() {
    echo "usage: $0 [-j N] [NAME...]" >&2
    exit 2
}

cpus=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
jobs=$((cpus * 2))
while getopts j:h opt; do
    case $opt in
        j) jobs=$OPTARG ;;
        *) usage ;;
    esac
done
shift $((OPTIND - 1))
case $jobs in '' | *[!0-9]* | 0*) usage ;; esac
limit=${HH27_TEST_TIMEOUT:-300}
case $limit in '' | *[!0-9]* | 0*) usage ;; esac

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

# The tests: name, then the command
names=(isp_test rv32_test)
cmds=("node tools/isp_test/isp_test.js" "sh tools/emu/rv32_test/run.sh")
for t in tools/*/run.sh; do
    n=$(basename "$(dirname "$t")")
    case " ${names[*]} " in
        *" $n "*) echo "$0: two tests named $n" >&2; exit 2 ;;
    esac
    names+=("$n")
    cmds+=("sh $t")
done

# Only the tests whose names start with an argument
if [ $# -gt 0 ]; then
    keep_names=() keep_cmds=()
    for i in "${!names[@]}"; do
        for f in "$@"; do
            if [[ ${names[i]} == "$f"* ]]; then
                keep_names+=("${names[i]}")
                keep_cmds+=("${cmds[i]}")
                break
            fi
        done
    done
    names=("${keep_names[@]+"${keep_names[@]}"}")
    cmds=("${keep_cmds[@]+"${keep_cmds[@]}"}")
    if [ ${#names[@]} -eq 0 ]; then
        echo "no test matches: $*" >&2
        exit 2
    fi
fi

ci=${GITHUB_ACTIONS:-}
# Python writes each line into the log at once: a stopped test shows where
export PYTHONUNBUFFERED=1
logs=$(mktemp -d)
trap 'rm -rf "$logs"' EXIT
# Each test in its own process group (set -m), so that Ctrl-C or a
# cancelled job stops every test with its emulators, and nothing else
set -m
pids=()
# Ctrl-C or a cancelled job: stop every test, and show the end of the log
# of each test that was still running, before the logs are removed
stop() {
    trap '' INT TERM
    local stopped=() name p
    for name in "${names[@]}"; do
        [ -f "$logs/$name.log" ] && [ ! -f "$logs/$name.result" ] && stopped+=("$name")
    done
    for p in "${pids[@]+"${pids[@]}"}"; do kill -- -"$p" 2>/dev/null; done
    echo
    echo "Stopped. Still running: ${stopped[*]-none}"
    for name in "${stopped[@]+"${stopped[@]}"}"; do
        echo "======== $name (stopped; the end of its log) ========"
        tail -n 40 "$logs/$name.log"
    done
    exit 130
}
trap stop INT TERM

now_ms() {
    local t=${EPOCHREALTIME//[!0-9]/}  # microseconds; the separator is the locale's
    echo $((10#$t / 1000))
}

echo "Building the emulator and the apps"
if ! make -s -C tools/emu || ! make -s -C apps >/dev/null; then
    echo "FAIL: the build" >&2
    exit 1
fi

# GNU timeout in the foreground: it stays in the test's process group
tmo=()
if timeout --version >/dev/null 2>&1; then
    tmo=(timeout --foreground -k 10 "$limit")
else
    echo "No GNU timeout: the tests run with no time limit" >&2
fi

# One test: its log, then "exit-code milliseconds" in name.result. It is the
# leader of its own process group (set -m): after the test, whatever the
# test left running (an emulator after a timeout, for one) is stopped too.
run_one() {
    local name=$1 cmd=$2 start rc ms
    start=$(now_ms)
    "${tmo[@]}" $cmd >"$logs/$name.log" 2>&1 </dev/null
    rc=$?
    ms=$(($(now_ms) - start))
    trap '' TERM
    kill -TERM -- -"$BASHPID" 2>/dev/null
    if [ "$rc" != 0 ] && [ ${#tmo[@]} -gt 0 ] && [ "$ms" -ge $((limit * 1000)) ]; then
        echo "TIMEOUT: stopped after $limit s (HH27_TEST_TIMEOUT)" >>"$logs/$name.log"
    fi
    echo "$rc $ms" >"$logs/$name.result"
}

start_all=$(now_ms)
echo "Running ${#names[@]} tests, $jobs at a time"
running=0
for i in "${!names[@]}"; do
    if [ "$running" -ge "$jobs" ]; then
        wait -n
        running=$((running - 1))
    fi
    run_one "${names[i]}" "${cmds[i]}" &
    pids+=($!)
    running=$((running + 1))
done
wait
wall=$(($(now_ms) - start_all))

# The logs: in CI the failures first and open, the rest folded; here the
# failures last, next to the table
failed=() passed=()
for name in "${names[@]}"; do
    read -r rc ms 2>/dev/null <"$logs/$name.result" || rc=1
    if [ "$rc" = 0 ]; then passed+=("$name"); else failed+=("$name"); fi
done
show() {
    local name=$1 how=$2
    if [ -n "$ci" ] && [ "$how" = fold ]; then
        echo "::group::$name"
        cat "$logs/$name.log"
        echo "::endgroup::"
    else
        [ -n "$ci" ] && [ "$how" = open ] && echo "::error::$name failed"
        echo "======== $name ========"
        cat "$logs/$name.log"
    fi
}
if [ -n "$ci" ]; then
    for n in "${failed[@]+"${failed[@]}"}"; do show "$n" open; done
    for n in "${passed[@]+"${passed[@]}"}"; do show "$n" fold; done
else
    for n in "${passed[@]+"${passed[@]}"}"; do show "$n" fold; done
    for n in "${failed[@]+"${failed[@]}"}"; do show "$n" open; done
fi

echo
printf '%-18s %-6s %8s\n' test result seconds
for name in "${names[@]}"; do
    rc=1 ms=0
    read -r rc ms 2>/dev/null <"$logs/$name.result"
    printf '%-18s %-6s %6d.%d\n' "$name" "$([ "$rc" = 0 ] && echo ok || echo FAIL)" $((ms / 1000)) $((ms % 1000 / 100))
done
printf '%-18s %-6s %6d.%d\n' "(wall)" "" $((wall / 1000)) $((wall % 1000 / 100))
if [ ${#failed[@]} -gt 0 ]; then
    echo "FAIL: ${failed[*]}"
    exit 1
fi
echo "ok: all ${#names[@]} tests"
