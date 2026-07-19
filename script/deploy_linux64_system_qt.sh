#!/bin/bash
set -e

rm -rf $DEST
mkdir -p $DEST

#### copy binary ####
cp $GITHUB_WORKSPACE/build/Gryph $DEST

#### copy Gryph.png ####
cp $GITHUB_WORKSPACE/res/public/Gryph.png $DEST

#### copy Core ####
cd download-artifact
cd *${DEST_SUFFIX%-system-qt}
tar xvzf artifacts.tgz -C ../../
cd ../..
cp deployment/${DEST_SUFFIX%-system-qt}/GryphCore $DEST
rm -rf deployment/${DEST_SUFFIX%-system-qt}

# handle debug info
objcopy --only-keep-debug $DEST/Gryph $DEST/Gryph.debug
strip --strip-debug --strip-unneeded $DEST/Gryph
objcopy --add-gnu-debuglink=$DEST/Gryph.debug $DEST/Gryph
