{
  description = "Make your m5stick a motion tracker";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs =
    inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];

      perSystem =
        { pkgs, ... }:
        {
          formatter = pkgs.alejandra;

          devShells.default = pkgs.mkShellNoCC {
            packages = with pkgs; [
              platformio
              python3
              pkg-config
            ];

            shellHook = ''
              export PLATFORMIO_HOME_DIR="$PWD/.pio"
            '';
          };
        };
    };
}
