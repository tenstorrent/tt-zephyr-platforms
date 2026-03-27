#!/bin/bash

# Copyright (c) 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

# Release Automation Script
# This script automates the release process using gh CLI

set -e  # Exit on any error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_step() {
    echo -e "${BLUE}[STEP]${NC} $1"
}

# Function to extract version info from VERSION file
extract_version_info() {
    local version_file=$1
    if [[ ! -f "$version_file" ]]; then
        print_error "VERSION file not found: $version_file"
        exit 1
    fi

    VERSION_MAJOR=$(grep "VERSION_MAJOR" "$version_file" | awk '{print $3}')
    VERSION_MINOR=$(grep "VERSION_MINOR" "$version_file" | awk '{print $3}')
    PATCHLEVEL=$(grep "PATCHLEVEL" "$version_file" | awk '{print $3}')
    VERSION_TWEAK=$(grep "VERSION_TWEAK" "$version_file" | awk '{print $3}')
    EXTRAVERSION=$(grep "EXTRAVERSION" "$version_file" | awk '{print $3}')

    print_status "Extracted version info from $version_file:"
    print_status "  VERSION_MAJOR: $VERSION_MAJOR"
    print_status "  VERSION_MINOR: $VERSION_MINOR"
    print_status "  PATCHLEVEL: $PATCHLEVEL"
    print_status "  VERSION_TWEAK: $VERSION_TWEAK"
    print_status "  EXTRAVERSION: '$EXTRAVERSION'"
}

# Function to bump version in a VERSION file
bump_version_minor() {
    local version_file=$1
    local app_name=$2

    if [[ ! -f "$version_file" ]]; then
        print_error "VERSION file not found: $version_file"
        exit 1
    fi

    # Extract current minor version
    local current_minor=$(grep "VERSION_MINOR" "$version_file" | awk '{print $3}')
    local new_minor=$((current_minor + 1))

    print_step "Bumping $app_name version from $current_minor to $new_minor"

    # Update the VERSION file
    sed -i .bak "s/^VERSION_MINOR = .*/VERSION_MINOR = $new_minor/" "$version_file"
    rm "${version_file}.bak"

    # Get the full version string for commit message
    local major=$(grep "VERSION_MAJOR" "$version_file" | awk '{print $3}')
    local tweak=$(grep "VERSION_TWEAK" "$version_file" | awk '{print $3}')
}

# Function to set PATCHLEVEL field
set_patchlevel() {
    local version_file=$1
    local app_name=$2
    local new_patchlevel=$3

    print_step "Setting PATCHLEVEL for $app_name to '$new_patchlevel'"
    sed -i .bak "s/^PATCHLEVEL = .*/PATCHLEVEL = $new_patchlevel/" "$version_file"
    rm "${version_file}.bak"
}

# Function to set EXTRAVERSION field
set_extraversion() {
    local version_file=$1
    local app_name=$2
    local extraversion_value=$3
    print_step "Setting EXTRAVERSION for $app_name to '$extraversion_value'"
    sed -i .bak "s/^EXTRAVERSION =.*/EXTRAVERSION = $extraversion_value/" "$version_file"
    rm "${version_file}.bak"
}

# Check if gh CLI is installed and authenticated
check_gh_cli() {
    if ! command -v gh &> /dev/null; then
        print_error "gh CLI could not be found. Please install it first."
        exit 1
    fi

    if ! gh auth status &> /dev/null; then
        print_error "gh CLI is not authenticated. Please run 'gh auth login' first."
        exit 1
    fi

    print_status "gh CLI is installed and authenticated"
}

# Check if we're in a git repository
check_git_repo() {
    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        print_error "Not in a git repository"
        exit 1
    fi

    print_status "In git repository"
}

main() {
    if [[ $# -ne 1 ]]; then
        print_error "Repository URL is required as an argument"
        show_help
        exit 1
    fi
    local repo_url=$1
    # If "git@" is not in the URL, error out
    if [[ ! $repo_url =~ ^git@github\.com:(.*)/(.*)\.git ]]; then
        print_error "Only SSH repository URLs supported, e.g. git@github.com/owner/repo.git"
        exit 1
    fi
    REPO_OWNER="${BASH_REMATCH[1]}"
    REPO_NAME="${BASH_REMATCH[2]}"
    print_step "Starting release automation process"

    # Preliminary checks
    check_gh_cli
    check_git_repo

    # Ensure we're on the main branch and up to date
    print_step "Ensuring we're on main branch and up to date"
    git checkout main
    git pull origin main

    # Extract version information from main VERSION file
    print_step "Reading main VERSION file"
    extract_version_info "VERSION"

    MAIN_VERSION_MAJOR=$VERSION_MAJOR
    MAIN_VERSION_MINOR=$((VERSION_MINOR + 1)) # Bump version minor for release branch

    # Create release branch name
    POST_RELEASE_BRANCH="release/post-${MAIN_VERSION_MAJOR}-${MAIN_VERSION_MINOR}"
    print_step "Creating post release branch: $POST_RELEASE_BRANCH"

    git checkout -b "$POST_RELEASE_BRANCH"

    # Bump DMC version
    print_step "Bumping DMC version"
    bump_version_minor "app/dmc/VERSION" "DMC"
    extract_version_info "app/dmc/VERSION"
    DMC_VERSION="$VERSION_MAJOR.$VERSION_MINOR.$PATCHLEVEL"
    git add app/dmc/VERSION
    git commit -sm "bump DMC version to $DMC_VERSION" -m "Set DMC VERSION to $DMC_VERSION"

    # Bump SMC version
    print_step "Bumping SMC version"
    bump_version_minor "app/smc/VERSION" "SMC"
    extract_version_info "app/smc/VERSION"
    SMC_VERSION="$VERSION_MAJOR.$VERSION_MINOR.$PATCHLEVEL"
    git add app/smc/VERSION
    git commit -sm "bump SMC version to $SMC_VERSION" -m "Set SMC VERSION to $SMC_VERSION"

    # Bump main VERSION
    print_step "Bumping main version"
    bump_version_minor "VERSION" "main"
    extract_version_info "VERSION"
    MAIN_VERSION="$VERSION_MAJOR.$VERSION_MINOR.$PATCHLEVEL"
    git add VERSION
    git commit -sm "bump main version to $MAIN_VERSION" -m "Set main VERSION to $MAIN_VERSION"

    FORK_ACCOUNT=$(gh auth status | grep "github.com account" | awk '{print $7}')
    FORK_URL="${repo_url/github.com:*\//github.com:$FORK_ACCOUNT/}"

    # Move back to main branch for version branch creation
    git checkout main

    # Create version branch
    VERSION_BRANCH="v${MAIN_VERSION_MAJOR}.${MAIN_VERSION_MINOR}-branch"
    print_step "Creating version branch: $VERSION_BRANCH"

    git checkout -b "$VERSION_BRANCH"

    # Clear PATCHLEVEL and create commits
    print_step "Setting PATCHLEVEL fields"

    # Set DMC PATCHLEVEL
    set_patchlevel "app/dmc/VERSION" "DMC" "0"
    bump_version_minor "app/dmc/VERSION" "DMC"
    set_extraversion "app/dmc/VERSION" "DMC" "rc1"
    extract_version_info "app/dmc/VERSION"
    DMC_FINAL_VERSION="$VERSION_MAJOR.$VERSION_MINOR.$VERSION_TWEAK-$EXTRAVERSION"
    git add app/dmc/VERSION
    git commit -sm "bump DMC version to $DMC_FINAL_VERSION" \
        -m "Set DMC VERSION to $DMC_FINAL_VERSION"

    # Set SMC PATCHLEVEL
    set_patchlevel "app/smc/VERSION" "SMC" "0"
    bump_version_minor "app/smc/VERSION" "SMC"
    set_extraversion "app/smc/VERSION" "SMC" "rc1"
    extract_version_info "app/smc/VERSION"
    SMC_FINAL_VERSION="$VERSION_MAJOR.$VERSION_MINOR.$VERSION_TWEAK-$EXTRAVERSION"
    git add app/smc/VERSION
    git commit -sm "bump SMC version to $SMC_FINAL_VERSION" \
        -m "Set SMC VERSION to $SMC_FINAL_VERSION"

    # Set main PATCHLEVEL
    set_patchlevel "VERSION" "main" "0"
    bump_version_minor "VERSION" "main"
    set_extraversion "VERSION" "main" "rc1"
    extract_version_info "VERSION"
    MAIN_FINAL_VERSION="$VERSION_MAJOR.$VERSION_MINOR.$VERSION_TWEAK-$EXTRAVERSION"
    git add VERSION
    git commit -sm "release: $MAIN_FINAL_VERSION" \
        -m "Set main VERSION to $MAIN_FINAL_VERSION for release $MAIN_FINAL_VERSION"

    # Create and push signed tag
    TAG_NAME="v$MAIN_FINAL_VERSION"
    print_step "Creating signed tag: $TAG_NAME"

    git tag -s "$TAG_NAME" -m "$REPO_NAME $TAG_NAME"

    if [[ -n "$DRY_RUN" ]]; then
        print_warning "Dry run mode enabled, skipping all pushes to $repo_url and $FORK_URL"
    else
        print_step "Pushing post release branch to fork: $FORK_URL"
        git push -u "$FORK_URL" "$POST_RELEASE_BRANCH"

        # Create PR for release branch
        print_step "Creating PR for post release branch"
        PR_TITLE="Post release $MAIN_VERSION_MAJOR.$MAIN_VERSION_MINOR"
        PR_BODY="Post release PR with version bumps:
- DMC version bumped to $DMC_VERSION
- SMC version bumped to $SMC_VERSION
- Main version bumped to $MAIN_VERSION"

        PR_URL=$(gh pr create --title "$PR_TITLE" --body "$PR_BODY" --base main \
            --repo "$REPO_OWNER/$REPO_NAME" --head $FORK_ACCOUNT:$POST_RELEASE_BRANCH)
        print_status "Created PR: $PR_URL"

        print_step "Pushing version branch to $repo_url"
        git push -u $repo_url "$VERSION_BRANCH"

        # Push the tag
        print_step "Pushing tag $TAG_NAME to $repo_url"
        git push -u $repo_url "$TAG_NAME"
    fi

    print_status "Release process completed successfully!"
    print_status "Summary:"
    print_status "  - Release branch created: $RELEASE_BRANCH"
    print_status "  - PR created: $PR_URL"
    print_status "  - Version branch created: $VERSION_BRANCH"
    print_status "  - Tag created: $TAG_NAME"
    print_status "  - DMC final version: $DMC_FINAL_VERSION"
    print_status "  - SMC final version: $SMC_FINAL_VERSION"
    print_status "  - Main final version: $MAIN_FINAL_VERSION"

    if [[ $DRY_RUN ]]; then
        print_warning "Dry run mode enabled, skipping RC process"
        exit 0
    fi

    # Now wait for the release tag to generate a draft release
    MESSAGE="Tag $TAG_NAME created. Would you like to wait for \
the draft release to be generated and then edit it? (y/n): "
    echo -n "$MESSAGE"
    read -r response
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        WARNING="Skipping waiting for draft release. \
You will need to publish the release manually from the GitHub UI."
        print_warning "$WARNING"
        exit 0
    fi

    # Wait for the draft release to be generated
    while True; do

        STATUS="Checking for draft release for tag $TAG_NAME... \
Release generation can take up to 30 minutes after pushing the tag."
        print_status "$STATUS"
        # This command can fail with a non-zero exit code if the release is not found
        set +e
        DRAFT_RELEASE_URL=$(gh release view "$TAG_NAME" --repo "$REPO_OWNER/$REPO_NAME" --json url --jq '.url')
        if [[ -n "$DRAFT_RELEASE_URL" ]]; then
            print_status "Draft release found: $DRAFT_RELEASE_URL"
            print_status "Please open the draft release URL and verify the release information!"
            set -e
            break
        else
            print_status "Draft release not found yet. Waiting 60 seconds before checking again..."
            sleep 60
        fi
    done

    echo "WARNING: Once you publish the RC release, you will not be able to make any changes to it"
    echo "Please make sure the following is correct before publishing:"
    echo "  - Release title is correct (should be $TAG_NAME)"
    echo "  - Release features a verified tag (look for the green 'Verified' badge next to the tag name)"
    echo "  - CI has passed on the tag commit"
    echo -n "Have you verified the draft release information and are ready to publish it? (y/n): "
    read -r publish_response
    if [[ ! "$publish_response" =~ ^[Yy]$ ]]; then
        print_warning "Release not published. Please publish the release manually from the GitHub UI when ready."
        exit 0
    fi
    # Publish the release using gh CLI
    print_step "Publishing release $TAG_NAME"
    gh release edit "$TAG_NAME" --repo "$REPO_OWNER/$REPO_NAME" --draft=false --prerelease=true
    print_status "Release $TAG_NAME published successfully!"
}

# Help function
show_help() {
    echo "Release Automation Script"
    echo ""
    echo "This script automates the release process by:"
    echo "  1. Creating a release branch (release/MAJOR-MINOR)"
    echo "  2. Bumping VERSION_MINOR in DMC, SMC, and main VERSION files"
    echo "  3. Creating individual commits for each version bump"
    echo "  4. Creating a PR for the version bumps to be merged into main"
    echo "  5. Creating a version branch (vMAJOR.MINOR-branch)"
    echo "  6. Setting EXTRAVERSION to 'rc1' for DMC, SMC, and main VERSION files"
    echo "  7. Bumping PATCHLEVEL for DMC, SMC, and main VERSION files, with individual commits"
    echo "  8. Pushing the version branch and creating a signed tag (vMAJOR.MINOR.TWEAK)"
    echo "  9. Waiting for the draft release to be generated and prompting the user to edit and publish it"
    echo ""
    echo "Prerequisites:"
    echo "  - gh CLI installed and authenticated"
    echo "  - Git repository with proper remote setup"
    echo "  - SSH key configured for signed tags"
    echo ""
    echo "Usage:"
    echo "  $0 REPO_URL   Run the release process for specified repository"
    echo "  $0 --help     Show this help message"
    echo "  $0 --dry-run REPO_URL Run through the process without pushing changes"
    echo ""
    echo "Repository URL format:"
    echo "  git@github.com:owner/repo.git"
    echo ""
}

# Parse command line arguments
case "${1:-}" in
    --help|-h)
        show_help
        exit 0
        ;;
    --dry-run)
        export DRY_RUN=true
        main "$2"
        ;;
    -*)
        print_error "Unknown option: $1"
        show_help
        exit 1
        ;;
    *)
        main "$1"
        ;;
esac
