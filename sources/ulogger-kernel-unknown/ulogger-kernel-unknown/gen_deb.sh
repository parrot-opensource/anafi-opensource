#!/bin/sh

# executing this script can be done from any location provided gen_deb.sh is
# in a ulog git repository
# Depends on realpath, git and dpkg
# usage: gen_deb.sh OUTPUT_DIR

set -eu

on_exit() {
	if [ $? != 0 ]; then
		echo "usage $(basename $0) OUTPUT_DIR\n\tCreates ulogger-dkms \
package in directory OUTPUT_DIR."
	fi
	if [ -n "${workdir+x}" ]; then
		rm -rf ${workdir}
	fi
}
trap on_exit EXIT

out_dir=$(realpath $1)
start_dir=$PWD
script_dir=$(dirname $0)
workdir=$(mktemp -d)

cd ${script_dir}

NAME="ulogger"
VERSION=$(git describe --abbrev=0 --tags | sed 's/ulog-//g')

dkms_dir="${workdir}/dkms"
install_dir="${workdir}/install"
src_dir="${workdir}/source"
ulogger_src_dir="${src_dir}/${NAME}-${VERSION}"

package="ulogger-dkms_${VERSION}"
input_deb_file="${dkms_dir}/${NAME}/${VERSION}/deb/${package}_all.deb"
output_deb_file="${out_dir}/${package}-1_all.deb"

DKMS_SETUP="--dkmstree ${dkms_dir} --sourcetree ${src_dir} --installtree ${install_dir}"
DKMS_MOD="-m $NAME -v $VERSION"
DKMS_ARG="$DKMS_SETUP $DKMS_MOD"

# Generate Debian source package of DKMS
mkdir -p "${dkms_dir}" "${install_dir}" "${src_dir}" "${ulogger_src_dir}"

cp dkms.conf *.c *.h Makefile ${ulogger_src_dir}

patched_files="${ulogger_src_dir}/dkms.conf"
# set the correct version number
for f in ${patched_files}; do
	sed -i "s/@@@VERSION@@@/${VERSION}/g" ${f}
done

dkms add $DKMS_ARG
#dkms build $DKMS_ARG
dkms mkdeb $DKMS_ARG --source-only

cd ${start_dir}

mv ${input_deb_file} ${output_deb_file}
