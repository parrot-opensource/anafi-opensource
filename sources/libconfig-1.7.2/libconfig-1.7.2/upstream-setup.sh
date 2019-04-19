#!/bin/bash

readonly UPSTREAM_REPO="https://github.com/hyperrealm/libconfig.git"

echo "Add upstream remote: ${UPSTREAM_REPO}"
git remote add upstream ${UPSTREAM_REPO}
git fetch upstream --no-tags

echo "Setup upstream branches"
git branch upstream-master upstream/master --track
