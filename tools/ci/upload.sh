#!/bin/bash
# From https://github.com/espressif/upload-components-ci-action 7115d054ca9cbba9213d7a2df33fe8822b97d481
#
# Removed ${,,} to be compatible with bash earlier than v4.0.
# SKIP_PRE_RELEASE and DRY_RUN must be set to 1 when active.


IFS=';' read -ra DIRECTORIES <<<"$(echo -e "${COMPONENTS_DIRECTORIES:-.}" | tr -d '[:space:]')"
NAMESPACE=${COMPONENTS_NAMESPACE:-espressif}
UPLOAD_ARGUMENTS=("--allow-existing" "--namespace=${NAMESPACE}" )
if [[ "${SKIP_PRE_RELEASE}" =~ ^(true|t|yes|1)$ ]]; then
    UPLOAD_ARGUMENTS+=("--skip-pre-release")
fi
if [[ "${DRY_RUN}" =~ ^(true|t|yes|1)$ ]]; then
    UPLOAD_ARGUMENTS+=("--dry-run")
fi

if [ -n "$COMPONENT_VERSION" ]; then
    if [ "$COMPONENT_VERSION" == "git" ]; then
        git fetch --force --tags
        if ! git describe --exact-match; then
            echo "Version is set to 'git', but the current commit is not tagged. Skipping the upload."
            exit 0
        fi
    fi
    UPLOAD_ARGUMENTS+=("--version=${COMPONENT_VERSION//v/}")
fi

NUMBER_OF_DIRECTORIES="${#DIRECTORIES[@]}"
echo "Processing $NUMBER_OF_DIRECTORIES components"

FAILED_COMPONENTS=()
for ITEM in "${DIRECTORIES[@]}"; do
    FULL_PATH="${GITHUB_WORKSPACE?}/${ITEM}"
    if [ "$NUMBER_OF_DIRECTORIES" -eq "1" ] && [ "${ITEM}" == "." ] && [ -z "${COMPONENT_NAME}" ]; then
        echo "To upload a single component, either specify the component name or directory, which will be used as the component name"
        exit 1
    fi

    if [ "${ITEM}" == "." ]; then
        NAME=${COMPONENT_NAME?"Name is required to upload a component from the root of the repository."}
    else
        NAME=$(basename "$(realpath "${FULL_PATH}")")
    fi

    echo "Processing component \"$NAME\" at $ITEM"

    PARAMS=("${UPLOAD_ARGUMENTS[@]}")
    PARAMS+=("--project-dir=${FULL_PATH}" "--name=${NAME}" )


    if [ -n "$REPOSITORY_URL" ]; then
      PARAMS+=("--repository=${REPOSITORY_URL}")
    fi

    if [ -n "$REPOSITORY_COMMIT_SHA" ]; then
      PARAMS+=("--commit-sha=${REPOSITORY_COMMIT_SHA}")
    fi

    if [ -n "$REPOSITORY_URL" ] && [ -n "$REPOSITORY_COMMIT_SHA" ]; then
      PARAMS+=("--repository-path=${ITEM}")
    fi

    echo ${PARAMS[@]}
    compote component upload "${PARAMS[@]}"

    EXIT_CODE=$?
    if [ "$EXIT_CODE" -ne "0" ]; then
        FAILED_COMPONENTS+=("${NAMESPACE}/${NAME}")
    fi
done

if [ ${#FAILED_COMPONENTS[@]} -ne 0 ]; then
    echo "Failed to upload the following components:"
    for COMPONENT in "${FAILED_COMPONENTS[@]}"; do
        echo "- ${COMPONENT}"
    done
    exit 1
fi
