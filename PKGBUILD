# Maintainer: Emil Renner Berthing <esmil@mailme.dk>

pkgname=lem-dbus
pkgver=0.3
pkgrel=1
pkgdesc='DBus library for the Lua Event Machine'
arch=('i686' 'x86_64' 'armv5tel' 'armv7l')
url='https://github.com/esmil/lem-dbus'
license=('GPL')
depends=('lem' 'dbus-core')
source=()

build() {
  cd "$startdir"

  make
}

package() {
  cd "$startdir"

  make DESTDIR="$pkgdir" install
}

# vim:set ts=2 sw=2 et:
