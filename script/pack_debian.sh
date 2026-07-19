#!/bin/bash
set -e

VERSION="$1"
ARCH="$2"

mkdir -p Gryph/DEBIAN
mkdir -p Gryph/opt
cp -r linux-$ARCH$([[ $3 == "systemqt" ]] && echo "-system-qt") Gryph/opt
mv Gryph/opt/linux-$ARCH$([[ $3 == "systemqt" ]] && echo "-system-qt") Gryph/opt/Gryph
rm Gryph/opt/Gryph/Gryph.debug

# basic
cat >Gryph/DEBIAN/control <<-EOF
Package: Gryph
Version: $VERSION
Architecture: $ARCH
Maintainer: Mahdi Mahdi.zrei@gmail.com
Depends: desktop-file-utils$([[ $3 == "systemqt" ]] && echo ", libqt6core6, libqt6gui6, libqt6network6, libqt6widgets6, qt6-qpa-plugins, qt6-wayland, qt6-gtk-platformtheme, qt6-xdgdesktopportal-platformtheme, libxcb-cursor0, fonts-noto-color-emoji")
Description: Qt based cross-platform GUI proxy configuration manager (backend: sing-box)
EOF

cat >Gryph/DEBIAN/postinst <<-EOF
cat >/usr/share/applications/Gryph.desktop<<-END
[Desktop Entry]
Name=Gryph
Comment=Qt based cross-platform GUI proxy configuration manager (backend: sing-box)
Exec=sh -c "PATH=/opt/Gryph:\$PATH /opt/Gryph/Gryph -appdata"
Icon=/opt/Gryph/Gryph.png
Terminal=false
Type=Application
Categories=Network;Application;
END

update-desktop-database
EOF

sudo chmod 0755 Gryph/DEBIAN/postinst

# desktop && PATH

sudo dpkg-deb --build Gryph
