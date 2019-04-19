# YMM: The way we integrate this project, we remove most of the magic...
autoreconf --force --install --verbose
rm -rf autom4te.cache
rm -f ac_config.h.in~
exit 0
