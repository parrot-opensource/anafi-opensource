#!/bin/bash

readonly UPSTREAM_REPO="git://git.gnome.org/glib"

echo "Add upstream remote: ${UPSTREAM_REPO}"
git remote add upstream ${UPSTREAM_REPO}
git fetch upstream --no-tags

echo "Setup upstream branches"
git branch upstream-master upstream/master --track
git branch upstream-2.32-branch upstream/glib-2-32 --track
git branch upstream-2.36-branch upstream/glib-2-36 --track
git branch upstream-2.44-branch upstream/glib-2-44 --track
git branch upstream-2.48-branch upstream/glib-2-48 --track
