#!/usr/bin/env bash
#
# run_tests.sh - Browser test harness runner
#
# Orchestrates reference script generation, engine dump mode execution,
# and JSON diff comparison across all test files.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BROWSER_BIN="${BROWSER:-$PROJECT_DIR/build/browser.exe}"
TESTS_DIR="$SCRIPT_DIR/tests"
RESULTS_DIR="$SCRIPT_DIR/results"
REF_HTML="$SCRIPT_DIR/reference_html.js"
REF_CSS="$SCRIPT_DIR/reference_css.js"
DIFF_ENGINE="$SCRIPT_DIR/json_diff.py"

mkdir -p "$RESULTS_DIR"
LATEST_RUN="$RESULTS_DIR/latest_run.txt"
LATEST_JSON="$RESULTS_DIR/latest_run.json"
HISTORY_FILE="$RESULTS_DIR/history.txt"
REF_ERRORS="$RESULTS_DIR/ref_errors.log"

# Flag for JSON output mode
JSON_OUTPUT=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --json) JSON_OUTPUT=true; shift ;;
        *) break ;;
    esac
done

# ---------------------------------------------------------------------------
# Prerequisite checks
# ---------------------------------------------------------------------------
check_deps() {
    local missing=0

    if ! command -v node &>/dev/null; then
        echo "ERROR: node not found in PATH" >&2
        missing=1
    fi

    PYTHON=python3
    if ! command -v "$PYTHON" &>/dev/null; then
        PYTHON=python
        if ! command -v "$PYTHON" &>/dev/null; then
            echo "ERROR: python3/python not found in PATH" >&2
            missing=1
        fi
    fi

    if [ ! -f "$BROWSER_BIN" ]; then
        echo "ERROR: browser binary not found at $BROWSER_BIN" >&2
        echo "  Set BROWSER env var or build the project first" >&2
        missing=1
    fi

    if [ ! -f "$REF_HTML" ]; then
        echo "ERROR: $REF_HTML not found" >&2
        missing=1
    fi

    if [ ! -f "$REF_CSS" ]; then
        echo "ERROR: $REF_CSS not found" >&2
        missing=1
    fi

    if [ ! -f "$DIFF_ENGINE" ]; then
        echo "ERROR: $DIFF_ENGINE not found" >&2
        missing=1
    fi

    if [ $missing -ne 0 ]; then
        exit 1
    fi

    # Install npm dependencies if needed
    if [ ! -d "$SCRIPT_DIR/node_modules" ]; then
        echo "Installing npm dependencies in tools/ ..."
        (cd "$SCRIPT_DIR" && npm install)
    fi

    # Check for timeout command
    TIMEOUT_CMD=""
    if command -v timeout &>/dev/null; then
        TIMEOUT_CMD="timeout"
    elif command -v gtimeout &>/dev/null; then
        TIMEOUT_CMD="gtimeout"
    fi
}

# ---------------------------------------------------------------------------
# Run a single test
# ---------------------------------------------------------------------------
# Echoes a CSV line: DOM,CSS,CASCADE,LAYOUT,DISP,OVERALL
# Also writes timing/diff JSON to temp files for JSON output mode.
run_single_test() {
    local test_file="$1"
    local basename
    basename="$(basename "$test_file")"
    local stem="${basename%.*}"

    local dom_result=" - "
    local css_result=" - "
    local cascade_result=" - "
    local layout_result=" - "
    local disp_result=" - "
    local overall="PASS"
    local critical=0
    local root_cause_stage=""
    local secondary_failures=""

    local ext="${basename##*.}"

    # Determine applicable dump modes based on file extension
    local do_dom=false
    local do_css=false
    local do_cascade=false
    local do_layout=false
    local do_display_list=false

    if [ "$ext" = "html" ]; then
        do_dom=true
        do_cascade=true
        do_layout=true
        do_display_list=true
    elif [ "$ext" = "css" ]; then
        do_css=true
    fi

    # JSON collection arrays
    local diff_entries_all=""
    local timing_parts=""
    local stage_results=""

    # Helper: run one pipeline stage
    run_stage() {
        local stage="$1"
        local dump_flag="$2"
        local ref_script="$3"
        local result_var_ref="$4"   # name of variable to set with result
        local timing_var_ref="$5"   # name of variable to set with timing ms

        local expected="$TESTS_DIR/${stem}.expected-${stage}.json"
        local actual="$TESTS_DIR/${stem}.actual-${stage}.json"

        # --- Reference generation ---
        if [ -n "$ref_script" ] && [ ! -f "$expected" ]; then
            local t0=0 t1=0
            t0=$($PYTHON -c 'import time; print(int(time.time()*1000))' 2>/dev/null || echo 0)
            if ! node "$ref_script" "$test_file" > "$expected" 2>>"$REF_ERRORS"; then
                printf -v "$result_var_ref" "REF_ERR"
                if [ "$overall" != "FAIL" ]; then overall="FAIL"; fi
                return
            fi
            t1=$($PYTHON -c 'import time; print(int(time.time()*1000))' 2>/dev/null || echo 0)
            local ref_ms=$((t1 - t0))
            timing_parts="$timing_parts\"${stage}_reference\":$ref_ms,"
        fi

        # If no expected file exists and ref script can't generate it, skip
        if [ ! -f "$expected" ]; then
            printf -v "$result_var_ref" "SKIP"
            return
        fi

        # --- Engine dump ---
        local t0_eng=0 t1_eng=0
        t0_eng=$($PYTHON -c 'import time; print(int(time.time()*1000))' 2>/dev/null || echo 0)
        local engine_exit=0
        if [ -n "$TIMEOUT_CMD" ]; then
            "$TIMEOUT_CMD" 10s "$BROWSER_BIN" "$dump_flag" "$test_file" > "$actual" 2>/dev/null || engine_exit=$?
        else
            "$BROWSER_BIN" "$dump_flag" "$test_file" > "$actual" 2>/dev/null || engine_exit=$?
        fi
        t1_eng=$($PYTHON -c 'import time; print(int(time.time()*1000))' 2>/dev/null || echo 0)
        local eng_ms=$((t1_eng - t0_eng))
        printf -v "$timing_var_ref" "$eng_ms"
        timing_parts="$timing_parts\"${stage}_engine\":$eng_ms,"

        if [ $engine_exit -eq 124 ]; then
            printf -v "$result_var_ref" "TIMEOUT"
            if [ "$overall" != "FAIL" ]; then overall="FAIL"; fi
            return
        elif [ $engine_exit -ne 0 ]; then
            printf -v "$result_var_ref" "ERR"
            if [ "$overall" != "FAIL" ]; then overall="FAIL"; fi
            return
        fi

        # --- Diff ---
        local diff_exit=0
        local diff_out
        diff_out=$($PYTHON "$DIFF_ENGINE" "$expected" "$actual" --mode "$stage" 2>&1) || diff_exit=$?

        echo "$diff_out" >> "$LATEST_RUN"

        if [ $diff_exit -eq 1 ]; then
            printf -v "$result_var_ref" "FAIL"
            critical=$((critical + 1))
            if [ -z "$root_cause_stage" ]; then
                root_cause_stage="$stage"
            else
                if [ -z "$secondary_failures" ]; then secondary_failures="$stage"; else secondary_failures="$secondary_failures,$stage"; fi
            fi
            if [ "$overall" != "FAIL" ]; then overall="FAIL"; fi
        elif [ $diff_exit -eq 2 ]; then
            printf -v "$result_var_ref" "MINOR"
        else
            printf -v "$result_var_ref" "PASS"
        fi
    }

    # ---- DOM ----
    if $do_dom; then run_stage "dom" "--dump-dom" "$REF_HTML" dom_result timing_dom_eng; fi
    # ---- CSS ----
    if $do_css; then run_stage "css" "--dump-css" "$REF_CSS" css_result timing_css_eng; fi
    # ---- CASCADE ----
    if $do_cascade; then run_stage "cascade" "--dump-cascade" "" cascade_result timing_cascade_eng; fi
    # ---- LAYOUT ----
    if $do_layout; then run_stage "layout" "--dump-layout" "" layout_result timing_layout_eng; fi
    # ---- DISPLAY LIST ----
    if $do_display_list; then run_stage "display-list" "--dump-display-list" "" disp_result timing_disp_eng; fi

    # Build JSON test object for --json mode
    if $JSON_OUTPUT; then
        # Build test object
        local tobj="{\"file\":\"$basename\",\"stages\":{\"dom\":\"$dom_result\",\"css\":\"$css_result\",\"cascade\":\"$cascade_result\",\"layout\":\"$layout_result\",\"display_list\":\"$disp_result\"},\"overall\":\"$overall\""
        if [ -n "$root_cause_stage" ]; then
            tobj="$tobj,\"root_cause_stage\":\"$root_cause_stage\""
            if [ -n "$secondary_failures" ]; then
                local sf_json=""
                IFS=',' read -ra sf_arr <<< "$secondary_failures"
                for s in "${sf_arr[@]}"; do
                    if [ -n "$sf_json" ]; then sf_json="$sf_json,"; fi
                    sf_json="$sf_json\"$s\""
                done
                tobj="$tobj,\"secondary_failures\":[$sf_json]"
            fi
        fi
        tobj="$tobj,\"timing_ms\":{${timing_parts%,}}}"

        # Store for later collection
        echo "$tobj" > "$RESULTS_DIR/_run_${stem}.json"
    fi

    echo "${dom_result},${css_result},${cascade_result},${layout_result},${disp_result},${overall}"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    check_deps

    # Clear latest run output
    > "$LATEST_RUN"
    > "$REF_ERRORS"

    echo "Running browser test harness..."
    echo "Browser binary: $BROWSER_BIN"
    echo "Tests directory: $TESTS_DIR"
    echo ""

    # Discover test files
    local test_files=()
    while IFS= read -r -d '' f; do
        test_files+=("$f")
    done < <(find "$TESTS_DIR" \( -name "*.html" -o -name "*.css" \) -print0 | sort -z)

    if [ ${#test_files[@]} -eq 0 ]; then
        echo "No test files found in $TESTS_DIR"
        exit 0
    fi

    echo "Found ${#test_files[@]} test files"
    echo ""

    # Print header
    printf "%-42s %-6s %-5s %-8s %-6s %-4s %s\n" "test" "DOM" "CSS" "CASCADE" "LAYOUT" "DISP" "RESULT"
    printf "%s\n" "----------------------------------------------------------------------------"

    local total=0
    local passed=0
    local failed=0
    local critical_failures=0

    # Arrays to hold per-test data for history / JSON
    local test_names=()
    local test_results=()

    for tf in "${test_files[@]}"; do
        local basename
        basename="$(basename "$tf")"
        local result_line
        result_line=$(run_single_test "$tf")
        IFS=',' read -r dom_res css_res cascade_res layout_res disp_res overall <<< "$result_line"

        test_names+=("$basename")
        test_results+=("$result_line")

        if [ "$overall" = "FAIL" ]; then
            failed=$((failed + 1))
            critical_failures=$((critical_failures + 1))
        elif [ "$overall" = "PASS" ]; then
            passed=$((passed + 1))
        else
            failed=$((failed + 1))
        fi
        total=$((total + 1))

        printf "%-42s %-6s %-5s %-8s %-6s %-4s %s\n" "$basename" "$dom_res" "$css_res" "$cascade_res" "$layout_res" "$disp_res" "$overall"
    done

    echo ""
    echo "Total: $total tests, $passed passed, $failed failed"
    echo "Critical failures: $critical_failures"

    # ---- Per-test history ----
    local timestamp_hist
    timestamp_hist=$(date "+%Y-%m-%d %H:%M:%S")
    for i in "${!test_names[@]}"; do
        IFS=',' read -r dom_res css_res cascade_res layout_res disp_res overall <<< "${test_results[$i]}"
        echo "$timestamp_hist | ${test_names[$i]} | dom=$dom_res css=$css_res cascade=$cascade_res layout=$layout_res disp=$disp_res | $overall" >> "$HISTORY_FILE"
    done

    # ---- JSON output ----
    if $JSON_OUTPUT; then
        local json_arr=""
        local first_test=true
        for tf in "${test_files[@]}"; do
            local basename
            basename="$(basename "$tf")"
            local stem="${basename%.*}"
            local tmp="$RESULTS_DIR/_run_${stem}.json"
            if [ -f "$tmp" ]; then
                if $first_test; then first_test=false; else json_arr="$json_arr,"; fi
                json_arr="$json_arr$(cat "$tmp")"
                rm -f "$tmp"
            fi
        done

        local timestamp
        timestamp=$(date -u "+%Y-%m-%dT%H:%M:%S" 2>/dev/null || date "+%Y-%m-%dT%H:%M:%S")

        cat > "$LATEST_JSON" <<EOF
{
  "timestamp": "$timestamp",
  "browser_binary": "$BROWSER_BIN",
  "total": $total,
  "passed": $passed,
  "failed": $failed,
  "critical": $critical_failures,
  "tests": [$json_arr]
}
EOF
        echo "JSON results written to: $LATEST_JSON"
    fi

    # Clean up temp files
    rm -f "$RESULTS_DIR"/_run_*.json

    if [ $critical_failures -gt 0 ]; then
        echo ""
        echo "Full diff output written to: $LATEST_RUN"
        exit 1
    fi
}

main "$@"
