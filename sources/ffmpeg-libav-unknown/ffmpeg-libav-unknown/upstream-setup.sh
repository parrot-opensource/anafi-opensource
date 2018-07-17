#!/bin/bash

readonly UPSTREAM_REPO="git://source.ffmpeg.org/ffmpeg.git"

echo "Add upstream remote: ${UPSTREAM_REPO}"
git remote add upstream ${UPSTREAM_REPO}
git fetch upstream --no-tags

echo "Setup upstream branches"
git branch upstream-master upstream/master --track
git branch upstream-2.8-branch upstream/release/2.8 --track
git branch upstream-3.2-branch upstream/release/3.2 --track
