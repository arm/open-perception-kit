#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

release_deletions() {
    local incoming="$1"
    jq -c --arg incoming "$incoming" '
        [.items[] |
            select(.versionName != $incoming) |
            select(.versionName | test("^v[0-9]+\\.[0-9]+\\.[0-9]+([-.+][0-9A-Za-z.-]+)?$"))] |
        sort_by(.createdAt // "", .versionName) |
        if length > 2 then .[0:(length - 2)][] else empty end'
}

if [ "${1:-}" = --self-test ]; then
    test "$#" -eq 1
    fixture='{"items":[
        {"versionName":"v0.1.0","createdAt":"2026-01-01"},
        {"versionName":"v0.2.0","createdAt":"2026-02-01"},
        {"versionName":"v0.3.0","createdAt":"2026-03-01"},
        {"versionName":"v0.4.0","createdAt":"2026-04-01"},
        {"versionName":"v0.5.0","createdAt":"2026-05-01"},
        {"versionName":"nightly","createdAt":"2026-06-01"}]}'
    mapfile -t deletions < <(release_deletions v0.5.0 <<< "$fixture")
    test "${#deletions[@]}" -eq 2
    test "$(jq -r '.versionName' <<< "${deletions[0]}")" = v0.1.0
    test "$(jq -r '.versionName' <<< "${deletions[1]}")" = v0.2.0
    mapfile -t deletions < <(release_deletions v0.6.0 <<< "$fixture")
    test "${#deletions[@]}" -eq 3
    echo "Black Duck release rotation self-test passed"
    exit 0
fi

test -n "${BLACKDUCK_TOKEN:-}"
test -n "${BLACKDUCK_URL:-}"
test -n "${GITHUB_REPOSITORY:-}"
test "$#" -eq 2

operation="$1"
requested_version="$2"
project_name="${GITHUB_REPOSITORY//\//:}"
project_id="781fdcd2-93c1-4b23-9ec0-358b0f6ee090"

auth_dir="$(mktemp -d)"
trap 'rm -rf -- "$auth_dir"' EXIT
curl --silent --show-error --fail \
    --request POST \
    --header "Authorization: token ${BLACKDUCK_TOKEN}" \
    --header 'Content-Type: application/json' \
    --dump-header "$auth_dir/headers" \
    --output "$auth_dir/body" \
    "${BLACKDUCK_URL%/}/api/tokens/authenticate"

bearer_token="$(jq -er '.bearerToken' "$auth_dir/body")"
csrf_token="$(awk 'tolower($1) == "x-csrf-token:" {gsub("\\r", "", $2); print $2}' "$auth_dir/headers")"
test -n "$csrf_token"
echo "::add-mask::${bearer_token}"
echo "::add-mask::${csrf_token}"
echo "Authenticated to Black Duck"
api_headers=(
    --silent
    --show-error
    --fail
    --header "Authorization: Bearer ${bearer_token}"
    --header "X-CSRF-TOKEN: ${csrf_token}"
    --header 'Accept: application/json'
    --header 'Content-Type: application/json'
)

project_url="${BLACKDUCK_URL%/}/api/projects/${project_id}"
project="$(curl "${api_headers[@]}" "$project_url")"
test "$(jq -r '.name' <<< "$project")" = "$project_name"
echo "Verified Black Duck project ${project_name}/${project_id}"

versions_url="$project_url/versions"
versions="$(curl "${api_headers[@]}" --get \
    --data-urlencode 'limit=100' \
    "$versions_url")"
# ponytail: the licence currently caps the project below 100 versions; paginate if that changes.
version_count="$(jq -er '.totalCount' <<< "$versions")"
test "$version_count" -le 100
echo "Found ${version_count} project versions"

delete_version() {
    local version="$1"
    local version_name version_url codelocations_url codelocations codelocation_count

    version_name="$(jq -r '.versionName' <<< "$version")"
    version_url="$(jq -r '._meta.href' <<< "$version")"
    codelocations_url="$(jq -er \
        '._meta.links[] | select(.rel == "codelocations") | .href' <<< "$version")"
    codelocations="$(curl "${api_headers[@]}" --get \
        --data-urlencode 'limit=100' \
        "$codelocations_url")"
    # ponytail: one version currently has at most four code locations; paginate if it can exceed 100.
    codelocation_count="$(jq -er '.totalCount' <<< "$codelocations")"
    test "$codelocation_count" -le 100
    while IFS= read -r codelocation_url; do
        curl "${api_headers[@]}" --request DELETE --output /dev/null "$codelocation_url"
    done < <(jq -r '.items[]._meta.href' <<< "$codelocations")

    curl "${api_headers[@]}" --request DELETE --output /dev/null "$version_url"
    echo "Deleted ${project_name}/${version_name}"
}

case "$operation" in
    delete)
        mapfile -t matches < <(
            jq -c --arg name "$requested_version" \
                '.items[] | select(.versionName == $name)' <<< "$versions"
        )
        test "${#matches[@]}" -le 1
        if [ "${#matches[@]}" -eq 0 ]; then
            echo "${project_name}/${requested_version} is already absent"
            exit 0
        fi
        delete_version "${matches[0]}"
        ;;
    rotate-release)
        [[ "$requested_version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+([-.+][0-9A-Za-z.-]+)?$ ]]
        while IFS= read -r version; do
            delete_version "$version"
        done < <(release_deletions "$requested_version" <<< "$versions")
        if ! jq -e --arg name "$requested_version" \
            '.items[] | select(.versionName == $name)' <<< "$versions" > /dev/null; then
            request="$(jq -cn --arg url "$versions_url" --arg name "$requested_version" \
                '{versionUrl: $url, cloneCategories: ["VULN_DATA", "COMPONENT_DATA"],
                  versionName: $name, phase: "DEVELOPMENT", distribution: "EXTERNAL"}')"
            curl "${api_headers[@]}" --request POST --data "$request" \
                --output /dev/null "$versions_url"
        fi
        echo "Reserved ${project_name}/${requested_version}; retaining it and two other releases"
        ;;
    *)
        echo "Usage: $0 {delete|rotate-release} VERSION" >&2
        exit 2
        ;;
esac
