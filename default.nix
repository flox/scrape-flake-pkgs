{ nixpkgs       ? builtins.getFlake "nixpkgs"
, system        ? builtins.currentSystem
, pkgsFor       ? nixpkgs.legacyPackages.${system}
, stdenv        ? pkgsFor.stdenv
, bash          ? pkgsFor.bash
, pkg-config    ? pkgsFor.pkg-config
, nix           ? pkgsFor.nix
, boost         ? pkgsFor.boost
, nlohmann_json ? pkgsFor.nlohmann_json
, darwin        ? pkgsFor.darwin
}: import ./pkg-fun.nix {
  inherit stdenv bash pkg-config nix boost nlohmann_json darwin;
}
