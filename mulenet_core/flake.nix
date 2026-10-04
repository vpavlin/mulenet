{
  description = "MuleNet core module: hub directory, onion labels, sealed grants, mailboxes, custody receipts (sync via loam_core). Headless AND the desktop view's backend.";

  inputs = {
    # Same pins as scala (full SHAs): loam_core facade + module-builder 0.2.6, one SDK via follows.
    logos-module-builder.url = "github:logos-co/logos-module-builder/2b59cb8e855894f7e7a064b15bfae409f288080b";
    loam_core.url = "github:vpavlin/loam-basecamp/24758d7acf5e8e3bfa77f935754d4e0129e5c017?dir=core";
    loam_core.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
