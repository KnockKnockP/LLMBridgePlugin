# GitAssetDiff.compact

Requires the separate `GitAssetDiff` plugin (`FGitAssetDiffModule`). That plugin owns the Git export/LFS/package-load logic and the Content Browser menu `Diff > Against Git Revision...`; this bridge only exposes it as tools.

Tools: `diff_asset_against_git_revision({asset_path,revision})` opens Unreal native diff for current asset vs Git revision. `inspect_blueprint_git_revision({asset_path,revision,include_nodes=false,...inspect_opts})` returns structured revision Blueprint JSON. `diff_blueprint_graph_against_git_revision({asset_path,revision,graph,include_nodes=false,include_unchanged=false})` returns agent-readable node diff for one graph.

Revision examples: `main,origin/main,HEAD~1,9f3c2ab`.

Flow (in GitAssetDiff): resolve package filename anchored to `.uproject`/Content for `/Game`, otherwise mounted/local package absolute path -> `git rev-parse --show-toplevel` -> repo-relative filename -> `git cat-file --filters <rev>:<path>` into `FPaths::DiffDir()` -> sidecars `.uexp,.ubulk,.uptnl` -> validate binary package vs LFS pointer/conflict text -> `DiffUtils::LoadPackageForDiff` with current-package mapping -> `IAssetTools::DiffAssets` (diff tool only).

Safety/invariants: no checkout/switch/working-tree mutation; revision copy loads only from diff temp dir; revision input limited to simple commit-ish chars; Git invoked with `safe.directory=<repo root>`.

Limits: asset package must exist on disk inside Git worktree; revision must contain valid binary Unreal package; dependencies come from current worktree.
