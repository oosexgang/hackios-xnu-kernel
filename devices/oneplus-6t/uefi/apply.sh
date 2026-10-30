#!/bin/bash
# Make ../Mu-Silicium with the OnePlus 6T (fajita) platform in it: the tree HakiOS's UEFI changes are made on comes from
# devices/poco-x5-pro/uefi/apply.sh (the same Mu-Silicium commit, BASE there), and fajitaPkg (files/ here) is put beside
# redwoodPkg. fajitaPkg is Mu-Silicium's enchiladaPkg (the OnePlus 6, the same SDM845 board) with the 6T's name, model and
# panel height; it takes its vendor drivers from Mu-Silicium's own Binaries/enchilada, so nothing is extracted from the phone.
set -e
cd "$(dirname "$(readlink -f "$0")")"
MU=../../../Mu-Silicium
[ -e $MU ] || ../../poco-x5-pro/uefi/apply.sh
[ -d $MU/Binaries/enchilada ] || { echo "uefi/apply.sh: $MU/Binaries/enchilada missing (git -C $MU submodule update --init Binaries)"; exit 1; }
cp -r files/. $MU/
echo "uefi/apply.sh: fajita added; next: cd Mu-Silicium && python3 build_uefi.py -d fajita -r RELEASE"
