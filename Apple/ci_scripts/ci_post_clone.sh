#!/bin/sh
# Xcode Cloud: regenerate the Xcode project from project.yml so the committed project can never be stale.
set -e
cd "$CI_PRIMARY_REPOSITORY_PATH/Apple"
command -v xcodegen >/dev/null 2>&1 || brew install xcodegen
xcodegen generate
