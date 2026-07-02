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

write_index_assets() {
    cat << 'EOF' > "${SITE_DIR}/report-index.css"
:root {
  --bg: #ffffff;
  --fg: #24292f;
  --muted: #57606a;
  --border: #d0d7de;
  --panel: #f6f8fa;
  --accent: #0969da;
  --pass: #1a7f37;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #0d1117;
    --fg: #e6edf3;
    --muted: #8b949e;
    --border: #30363d;
    --panel: #161b22;
    --accent: #58a6ff;
    --pass: #3fb950;
  }
}
body {
  margin: 0;
  background: var(--bg);
  color: var(--fg);
  font: 14px -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif;
}
main {
  max-width: 1040px;
  margin: 0 auto;
  padding: 32px 24px 48px;
}
header {
  border-bottom: 1px solid var(--border);
  margin-bottom: 24px;
  padding-bottom: 20px;
}
h1 {
  font-size: 28px;
  line-height: 36px;
  margin: 0;
}
h2 {
  font-size: 18px;
  line-height: 28px;
  margin: 0 0 12px;
}
p {
  color: var(--muted);
  margin: 8px 0 0;
}
section {
  margin-top: 24px;
}
.eyebrow {
  color: var(--muted);
  font-size: 12px;
  font-weight: 600;
  letter-spacing: 0;
  margin-bottom: 6px;
  text-transform: uppercase;
}
.report-list {
  border: 1px solid var(--border);
  border-radius: 6px;
  overflow: hidden;
}
.report-link,
.empty {
  align-items: center;
  background: var(--panel);
  border-top: 1px solid var(--border);
  display: flex;
  gap: 16px;
  justify-content: space-between;
  padding: 14px 16px;
}
.report-link:first-child,
.empty:first-child {
  border-top: 0;
}
.report-link {
  color: var(--fg);
  text-decoration: none;
}
.report-link:hover {
  background: var(--bg);
}
.report-title {
  display: block;
  font-weight: 600;
  overflow-wrap: anywhere;
}
.report-meta {
  color: var(--muted);
  display: block;
  font-size: 12px;
  margin-top: 2px;
}
.badge {
  border: 1px solid var(--border);
  border-radius: 12px;
  color: var(--pass);
  flex: none;
  font-size: 12px;
  font-weight: 600;
  padding: 2px 8px;
}
.back-link {
  color: var(--accent);
  display: inline-block;
  margin-top: 18px;
}
@media (max-width: 640px) {
  main {
    padding: 20px 12px 32px;
  }
  h1 {
    font-size: 22px;
    line-height: 30px;
  }
  .report-link,
  .empty {
    align-items: flex-start;
    flex-direction: column;
    gap: 8px;
    padding: 12px;
  }
  .badge {
    align-self: flex-start;
  }
}
EOF
}

write_report_shell_assets() {
    cat << 'EOF' > "${SITE_DIR}/report-shell.css"
:root {
  --pek-report-bg: #ffffff;
  --pek-report-fg: #24292f;
  --pek-report-muted: #57606a;
  --pek-report-border: #d0d7de;
  --pek-report-panel: #f6f8fa;
  --pek-report-accent: #0969da;
}
@media (prefers-color-scheme: dark) {
  :root {
    --pek-report-bg: #0d1117;
    --pek-report-fg: #e6edf3;
    --pek-report-muted: #8b949e;
    --pek-report-border: #30363d;
    --pek-report-panel: #161b22;
    --pek-report-accent: #58a6ff;
  }
}
.pek-report-bar {
  background: var(--pek-report-bg);
  border-bottom: 1px solid var(--pek-report-border);
  color: var(--pek-report-fg);
  font: 14px -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif;
  padding: 16px 24px;
}
.pek-report-bar-inner {
  align-items: center;
  display: flex;
  gap: 16px;
  justify-content: space-between;
  margin: 0 auto;
  max-width: 1200px;
}
.pek-report-title {
  font-size: 20px;
  font-weight: 600;
  line-height: 28px;
  overflow-wrap: anywhere;
}
.pek-report-meta {
  color: var(--pek-report-muted);
  font-size: 12px;
  line-height: 18px;
}
.pek-report-back {
  background: var(--pek-report-panel);
  border: 1px solid var(--pek-report-border);
  border-radius: 6px;
  color: var(--pek-report-accent);
  flex: none;
  padding: 6px 10px;
  text-decoration: none;
}
.pek-report-back:hover {
  background: var(--pek-report-bg);
}
@media (max-width: 640px) {
  .pek-report-bar {
    padding: 12px;
  }
  .pek-report-bar-inner {
    align-items: flex-start;
    flex-direction: column;
    gap: 10px;
  }
}
EOF
}

write_index_head() {
    local title="$1"
    local css_href="$2"

    cat << EOF
<!doctype html>
<html lang="en" style="scrollbar-gutter: stable both-edges;">
  <head>
    <meta charset="utf-8">
    <meta name="color-scheme" content="dark light">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>$(printf '%s' "${title}" | html_escape)</title>
    <link rel="stylesheet" href="$(printf '%s' "${css_href}" | html_escape)">
  </head>
  <body>
    <main>
EOF
}

write_index_footer() {
    cat << 'EOF'
    </main>
  </body>
</html>
EOF
}

write_report_index() {
    local report_dir="$1"
    local title="$2"
    local back_href="$3"
    local phase=""

    {
        write_index_head "${title}" "${back_href}report-index.css"
        cat << EOF
      <header>
        <div class="eyebrow">Playwright</div>
        <h1>$(printf '%s' "${title}" | html_escape)</h1>
        <p>Open a report section below.</p>
      </header>
      <section>
        <h2>Report sections</h2>
        <div class="report-list">
EOF
        for phase in "${report_dir}"/*; do
            [ -f "${phase}/index.html" ] || continue
            phase="${phase##*/}"
            printf '          <a class="report-link" href="%s/"><span><span class="report-title">%s</span><span class="report-meta">Playwright report</span></span><span class="badge">Open</span></a>\n' \
                "$(printf '%s' "${phase}" | html_escape)" \
                "$(printf '%s' "${phase}" | html_escape)"
        done
        cat << 'EOF'
        </div>
      </section>
EOF
        printf '      <a class="back-link" href="%s">Back to report index</a>\n' \
            "$(printf '%s' "${back_href}" | html_escape)"
        write_index_footer
    } > "${report_dir}/index.html"
}

write_site_index() {
    local pr_dir=""
    local pr_number=""

    {
        write_index_head "PEK Playwright Reports" "report-index.css"
        cat << 'EOF'
      <header>
        <div class="eyebrow">Perception Experience Kit</div>
        <h1>Playwright reports</h1>
        <p>Published browser smoke reports with video attachments.</p>
      </header>
      <section>
        <h2>Nightly</h2>
        <div class="report-list">
EOF
        if [ -f "${SITE_DIR}/nightly/index.html" ]; then
            echo '          <a class="report-link" href="nightly/"><span><span class="report-title">Latest nightly</span><span class="report-meta">Scheduled main run</span></span><span class="badge">Open</span></a>'
        else
            echo '          <div class="empty">No nightly report published yet.</div>'
        fi
        cat << 'EOF'
        </div>
      </section>
      <section>
        <h2>Pull Requests</h2>
        <div class="report-list">
EOF
        if [ -d "${SITE_DIR}/prs" ]; then
            find "${SITE_DIR}/prs" -mindepth 1 -maxdepth 1 -type d |
                sort -V |
                while IFS= read -r pr_dir; do
                    pr_number="${pr_dir##*/}"
                    [ -f "${pr_dir}/index.html" ] || continue
                    printf '          <a class="report-link" href="prs/%s/"><span><span class="report-title">PR #%s</span><span class="report-meta">Latest published pull request report</span></span><span class="badge">Open</span></a>\n' \
                        "$(printf '%s' "${pr_number}" | html_escape)" \
                        "$(printf '%s' "${pr_number}" | html_escape)"
                done
        fi
        cat << 'EOF'
        </div>
      </section>
EOF
        write_index_footer
    } > "${SITE_DIR}/index.html"
}

decorate_playwright_report() {
    local report_dir="$1"
    local title="$2"
    local back_href="$3"
    local meta="$4"
    local css_href=""
    local index_file="${report_dir}/index.html"
    local tmp_file=""

    [ -f "${index_file}" ] || return
    grep -q 'class="pek-report-bar"' "${index_file}" && return

    css_href="${back_href}report-shell.css"
    tmp_file="$(mktemp)"
    awk \
        -v css_href="$(printf '%s' "${css_href}" | html_escape)" \
        -v title="$(printf '%s' "${title}" | html_escape)" \
        -v back_href="$(printf '%s' "${back_href}" | html_escape)" \
        -v meta="$(printf '%s' "${meta}" | html_escape)" \
        '
        /<\/head>/ && !linked {
            print "    <link rel=\"stylesheet\" href=\"" css_href "\">"
            linked = 1
        }
        { print }
        /<body[^>]*>/ && !decorated {
            print "    <div class=\"pek-report-bar\"><div class=\"pek-report-bar-inner\"><div><div class=\"pek-report-title\">" title "</div><div class=\"pek-report-meta\">" meta "</div></div><a class=\"pek-report-back\" href=\"" back_href "\">Back to report index</a></div></div>"
            decorated = 1
        }
        ' "${index_file}" > "${tmp_file}"
    mv "${tmp_file}" "${index_file}"
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
    local back_href=""
    local meta=""
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
        back_href="../../"
    else
        if [ "${UPSTREAM_EVENT}" != "schedule" ] || [ "${UPSTREAM_HEAD_BRANCH}" != "main" ]; then
            echo "Skipping non-PR Playwright report from ${UPSTREAM_EVENT} on ${UPSTREAM_HEAD_BRANCH}."
            set_output deploy false
            return
        fi
        target="${SITE_DIR}/nightly"
        title="Nightly Playwright report"
        back_href="../"
    fi
    meta="${UPSTREAM_HEAD_BRANCH} @ ${UPSTREAM_HEAD_SHA:0:12} | run ${UPSTREAM_RUN_ID} attempt ${UPSTREAM_RUN_ATTEMPT}"

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
    write_report_shell_assets
    if [ ! -f "${target}/index.html" ]; then
        write_report_index "${target}" "${title}" "${back_href}"
    else
        decorate_playwright_report "${target}" "${title}" "${back_href}" "${meta}"
    fi
    printf '%s\n' "${UPSTREAM_HEAD_SHA}" > "${target}/commit.txt"
    touch "${SITE_DIR}/.nojekyll"
    write_index_assets
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
        write_index_assets
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
