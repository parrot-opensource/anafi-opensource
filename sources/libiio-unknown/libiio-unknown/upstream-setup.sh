#!/bin/bash

readonly UPSTREAM_REPO="https://github.com/analogdevicesinc/libiio.git"
readonly GITHUB_REPO="https://github.com/Parrot-Developers/libiio.git"

echo "Add upstream remote: ${UPSTREAM_REPO}"
git remote add upstream ${UPSTREAM_REPO}
git fetch upstream --no-tags

echo "Setup upstream branches"
git branch upstream-master upstream/master --track

echo "Add github remote: ${GITHUB_REPO}"
git remote add github ${GITHUB_REPO}
git fetch github --no-tags

echo "Setup github branches"
git branch github-master github/master --track

