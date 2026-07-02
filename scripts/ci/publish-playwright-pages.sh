#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
# Updates the persisted Playwright report site used by GitHub Pages.
################################################################

set -euo pipefail

MODE="${1:-}"
STORAGE_BRANCH="${PLAYWRIGHT_PAGES_STORAGE_BRANCH:-playwright-pages}"
SITE_DIR="${PLAYWRIGHT_PAGES_SITE_DIR:-_playwright_pages_site}"
RETENTION_DAYS="${PLAYWRIGHT_PAGES_RETENTION_DAYS:-10}"

usage() {
    echo "Usage: scripts/ci/publish-playwright-pages.sh publish|cleanup" >&2
}

require_env() {
    local name="$1"

    if [ -z "${!name:-}" ]; then
        echo "Error: ${name} is required." >&2
        exit 1
    fi
}

set_output() {
    local name="$1"
    local value="$2"

    if [ -n "${GITHUB_OUTPUT:-}" ]; then
        printf '%s=%s\n' "${name}" "${value}" >> "${GITHUB_OUTPUT}"
    fi
}

git_auth_header() {
    require_env GITHUB_TOKEN
    printf 'x-access-token:%s' "${GITHUB_TOKEN}" | base64 | tr -d '\n'
}

checkout_site_branch() {
    local auth_header=""

    require_env GITHUB_REPOSITORY
    auth_header="$(git_auth_header)"
    echo "::add-mask::${auth_header}"

    rm -rf "${SITE_DIR}"
    mkdir -p "${SITE_DIR}"
    git -C "${SITE_DIR}" init
    git -C "${SITE_DIR}" remote add origin "https://github.com/${GITHUB_REPOSITORY}.git"

    if git -C "${SITE_DIR}" \
        -c "http.https://github.com/.extraheader=AUTHORIZATION: basic ${auth_header}" \
        fetch --depth=1 origin "${STORAGE_BRANCH}" > /dev/null 2>&1; then
        git -C "${SITE_DIR}" checkout -B "${STORAGE_BRANCH}" FETCH_HEAD
    else
        git -C "${SITE_DIR}" checkout --orphan "${STORAGE_BRANCH}"
        git -C "${SITE_DIR}" rm -rf . > /dev/null 2>&1 || true
    fi

    git -C "${SITE_DIR}" config user.name "github-actions[bot]"
    git -C "${SITE_DIR}" config user.email "41898282+github-actions[bot]@users.noreply.github.com"
}

push_site_branch() {
    local auth_header=""

    git -C "${SITE_DIR}" add -A .
    if git -C "${SITE_DIR}" diff --cached --quiet; then
        return 2
    fi

    auth_header="$(git_auth_header)"
    git -C "${SITE_DIR}" commit -m "Update Playwright report pages"
    git -C "${SITE_DIR}" \
        -c "http.https://github.com/.extraheader=AUTHORIZATION: basic ${auth_header}" \
        push origin "HEAD:${STORAGE_BRANCH}"
}

push_or_skip_site_branch() {
    local status=0

    push_site_branch || status=$?
    [ "${status}" -eq 0 ] || [ "${status}" -eq 2 ] || return "${status}"
}

html_escape() {
    sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g'
}

write_report_index() {
    local report_dir="$1"
    local title="$2"
    local back_href="$3"
    local phase=""

    {
        cat << EOF
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <title>$(printf '%s' "${title}" | html_escape)</title>
  </head>
  <body>
    <h1>$(printf '%s' "${title}" | html_escape)</h1>
    <ul>
EOF
        for phase in "${report_dir}"/*; do
            [ -f "${phase}/index.html" ] || continue
            phase="${phase##*/}"
            printf '      <li><a href="%s/">%s</a></li>\n' \
                "$(printf '%s' "${phase}" | html_escape)" \
                "$(printf '%s' "${phase}" | html_escape)"
        done
        cat << 'EOF'
    </ul>
EOF
        printf '    <p><a href="%s">Back to report index</a></p>\n' \
            "$(printf '%s' "${back_href}" | html_escape)"
        cat << 'EOF'
  </body>
</html>
EOF
    } > "${report_dir}/index.html"
}

write_site_index() {
    local pr_dir=""
    local pr_number=""

    {
        cat << 'EOF'
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <title>PEK Playwright Reports</title>
  </head>
  <body>
    <h1>PEK Playwright Reports</h1>
    <h2>Nightly</h2>
    <ul>
EOF
        if [ -f "${SITE_DIR}/nightly/index.html" ]; then
            echo '      <li><a href="nightly/">latest nightly</a></li>'
        else
            echo '      <li>No nightly report published yet.</li>'
        fi
        cat << 'EOF'
    </ul>
    <h2>Pull Requests</h2>
    <ul>
EOF
        if [ -d "${SITE_DIR}/prs" ]; then
            find "${SITE_DIR}/prs" -mindepth 1 -maxdepth 1 -type d |
                sort -V |
                while IFS= read -r pr_dir; do
                    pr_number="${pr_dir##*/}"
                    [ -f "${pr_dir}/index.html" ] || continue
                    printf '      <li><a href="prs/%s/">PR #%s</a></li>\n' \
                        "$(printf '%s' "${pr_number}" | html_escape)" \
                        "$(printf '%s' "${pr_number}" | html_escape)"
                done
        fi
        cat << 'EOF'
    </ul>
  </body>
</html>
EOF
    } > "${SITE_DIR}/index.html"
}

download_report_artifact() {
    local artifact_name=""
    local artifact_dir="$1"

    require_env UPSTREAM_RUN_ID
    require_env UPSTREAM_RUN_ATTEMPT

    artifact_name="rpi-browser-smoke-${UPSTREAM_RUN_ID}-${UPSTREAM_RUN_ATTEMPT}"
    if gh run download "${UPSTREAM_RUN_ID}" \
        --repo "${GITHUB_REPOSITORY}" \
        --name "${artifact_name}" \
        --dir "${artifact_dir}" > /dev/null 2>&1; then
        return
    fi

    echo "No Playwright browser smoke artifact found for run ${UPSTREAM_RUN_ID} attempt ${UPSTREAM_RUN_ATTEMPT}."
    return 1
}

prune_report_for_pages() {
    local report_dir="$1"

    # GitHub rejects large git blobs; videos/screenshots stay, trace zips do not.
    find "${report_dir}" -path '*/data/*.zip' -type f -delete
}

publish_report() {
    local artifact_dir=""
    local report_dir=""
    local target=""
    local title=""

    require_env GITHUB_REPOSITORY
    require_env UPSTREAM_EVENT
    require_env UPSTREAM_HEAD_BRANCH
    require_env UPSTREAM_HEAD_SHA
    require_env UPSTREAM_CONCLUSION

    case "${UPSTREAM_CONCLUSION}" in
        success | failure) ;;
        *)
            echo "Skipping Playwright report from ${UPSTREAM_CONCLUSION} upstream run."
            set_output deploy false
            return
            ;;
    esac

    if [ "${UPSTREAM_EVENT}" = "pull_request" ]; then
        if [ -n "${UPSTREAM_HEAD_REPOSITORY:-}" ] &&
            [ "${UPSTREAM_HEAD_REPOSITORY}" != "${GITHUB_REPOSITORY}" ]; then
            echo "Skipping PR Playwright report from untrusted repository: ${UPSTREAM_HEAD_REPOSITORY}."
            set_output deploy false
            return
        fi
        if [ -z "${UPSTREAM_PR_NUMBER:-}" ]; then
            echo "No PR number found for upstream run; skipping Pages publish."
            set_output deploy false
            return
        fi
        target="${SITE_DIR}/prs/${UPSTREAM_PR_NUMBER}"
        title="PR #${UPSTREAM_PR_NUMBER} Playwright report"
    else
        if [ "${UPSTREAM_EVENT}" != "schedule" ] || [ "${UPSTREAM_HEAD_BRANCH}" != "main" ]; then
            echo "Skipping non-PR Playwright report from ${UPSTREAM_EVENT} on ${UPSTREAM_HEAD_BRANCH}."
            set_output deploy false
            return
        fi
        target="${SITE_DIR}/nightly"
        title="Nightly Playwright report"
    fi

    artifact_dir="$(mktemp -d)"
    if ! download_report_artifact "${artifact_dir}"; then
        set_output deploy false
        return
    fi

    report_dir="$(find "${artifact_dir}" -type d -name playwright-report -print -quit)"
    if [ -z "${report_dir}" ]; then
        echo "Artifact did not contain playwright-report; skipping Pages publish."
        set_output deploy false
        return
    fi

    checkout_site_branch
    rm -rf "${target}"
    mkdir -p "${target}"
    cp -a "${report_dir}/." "${target}/"
    prune_report_for_pages "${target}"
    if [ ! -f "${target}/index.html" ]; then
        if [ "${UPSTREAM_EVENT}" = "pull_request" ]; then
            write_report_index "${target}" "${title}" "../../"
        else
            write_report_index "${target}" "${title}" "../"
        fi
    fi
    printf '%s\n' "${UPSTREAM_HEAD_SHA}" > "${target}/commit.txt"
    touch "${SITE_DIR}/.nojekyll"
    write_site_index

    push_or_skip_site_branch
    set_output deploy true
}

cleanup_closed_pr_reports() {
    local cutoff=""
    local pr_dir=""
    local pr_number=""
    local pr_json=""
    local state=""
    local closed_at=""
    local closed_epoch=""
    local changed="false"

    require_env GITHUB_REPOSITORY

    checkout_site_branch
    cutoff="$(date -u -d "${RETENTION_DAYS} days ago" +%s)"

    for pr_dir in "${SITE_DIR}"/prs/*; do
        [ -d "${pr_dir}" ] || continue
        pr_number="${pr_dir##*/}"
        [[ "${pr_number}" =~ ^[0-9]+$ ]] || continue

        pr_json="$(gh pr view "${pr_number}" --repo "${GITHUB_REPOSITORY}" --json state,closedAt 2> /dev/null || true)"
        [ -n "${pr_json}" ] || continue

        state="$(jq -r '.state' <<< "${pr_json}")"
        closed_at="$(jq -r '.closedAt // empty' <<< "${pr_json}")"
        [ "${state}" != "OPEN" ] || continue
        [ -n "${closed_at}" ] || continue

        closed_epoch="$(date -u -d "${closed_at}" +%s)"
        if [ "${closed_epoch}" -le "${cutoff}" ]; then
            rm -rf "${pr_dir}"
            changed="true"
        fi
    done

    if [ "${changed}" = "true" ]; then
        write_site_index
        push_or_skip_site_branch
        set_output deploy true
    else
        set_output deploy false
    fi
}

case "${MODE}" in
    publish)
        publish_report
        ;;
    cleanup)
        cleanup_closed_pr_reports
        ;;
    *)
        usage
        exit 2
        ;;
esac
