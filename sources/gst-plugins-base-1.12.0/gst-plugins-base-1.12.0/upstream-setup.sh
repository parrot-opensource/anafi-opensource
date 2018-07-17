#!/bin/bash

readonly UPSTREAM_REPO="git://anongit.freedesktop.org/gstreamer/gst-plugins-base"

echo "Add upstream remote: ${UPSTREAM_REPO}"
git remote add upstream ${UPSTREAM_REPO}
git fetch upstream --no-tags

echo "Setup upstream branches"
git branch upstream-master upstream/master --track
git branch upstream-1.0-branch upstream/1.0 --track
git branch upstream-1.4-branch upstream/1.4 --track
git branch upstream-1.6-branch upstream/1.6 --track
git branch upstream-1.8-branch upstream/1.8 --track
git branch upstream-1.10-branch upstream/1.10 --track
git branch upstream-1.12-branch upstream/1.12 --track
