{ nixpkgs       ? builtins.getFlake "nixpkgs"
, system        ? builtins.currentSystem
, pkgsFor       ? nixpkgs.legacyPackages.${system}
, stdenv        ? pkgsFor.stdenv
, bash          ? pkgsFor.bash
, nix           ? pkgsFor.nix
, boost         ? pkgsFor.boost
, nlohmann_json ? pkgsFor.nlohmann_json
, darwin        ? pkgsFor.darwin
}: import ./pkg-fun.nix { inherit stdenv bash nix boost nlohmann_json darwin; }
