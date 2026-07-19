#!/bin/bash
set -e

rm -rf $DEST
mkdir -p $DEST

#### copy golang => .app ####
cd download-artifact
cd *$DEST_SUFFIX
tar xvzf artifacts.tgz -C ../../
cd ../..

mv deployment/$DEST_SUFFIX/* $GITHUB_WORKSPACE/build/Gryph.app/Contents/MacOS

#### deploy qt & Dylib runtime => .app ####
pushd $GITHUB_WORKSPACE/build
macdeployqt Gryph.app -verbose=3
popd

codesign --force --deep --sign - $GITHUB_WORKSPACE/build/Gryph.app

dsymutil $GITHUB_WORKSPACE/build/Gryph.app/Contents/MacOS/Gryph
strip -S $GITHUB_WORKSPACE/build/Gryph.app/Contents/MacOS/Gryph

mv $GITHUB_WORKSPACE/build/Gryph.app $DEST
