#!/bin/bash -e

# used pip packages
pip_packages="jupyter matplotlib numpy"
target_dir=./docs/examples/

test_body() {
    test_files=("general/data_loading/numpy_reader.ipynb")

    # GDS can't read data from the Docker filesystem.
    # Here we are copying the relevant part of DALI_extra that we need to run the notebook
    # to a "real" filesystem
    tmpdir=`mktemp -d "/scratch/numpy_reader_notebook_XXXXXX" 2>/dev/null`
    NEW_DALI_EXTRA=${tmpdir}/DALI_extra
    mkdir -p ${NEW_DALI_EXTRA}/db/3D/MRI/Knee
    cp -r ${DALI_EXTRA_PATH}/db/3D/MRI/Knee/npy_* ${NEW_DALI_EXTRA}/db/3D/MRI/Knee/
    export DALI_EXTRA_PATH=${NEW_DALI_EXTRA}

    # test code
    echo $test_files | xargs -i jupyter nbconvert \
                   --to notebook --inplace --execute \
                   --ExecutePreprocessor.kernel_name=python${PYVER:0:1} \
                   --ExecutePreprocessor.timeout=600 {}

    # cleanup
    rm -rf ${tmpdir}
}

pushd ../..
source ./qa/test_template.sh
popd
