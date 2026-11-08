# Workflow

How this repository applies the [commit skill](../.agents/skills/commit/SKILL.md).

## Stack

The stack lives on a detached HEAD, managed with git-branchless. Below, `<target>` stands for the target branch. Rewrite history in memory, with `git amend`, `git reword`, and `git move`. Never start `git rebase`.

- To fold a review fix, stage it, and run `git absorb --force-detach --base <target>` to make fixup commits. git-branchless tracks them only while HEAD is on the last one, so fold them before you check out anything else. Fold each into its target with `git move --fixup`. Each `git move` rewrites the descendants of its target, so look up the hashes again before the next one. `git move` leaves HEAD on the target, so check out the new tip, and compare it with the old one.
- `git absorb` leaves a hunk staged when no commit of the stack changed its lines. Commit it with `git commit --fixup=<commit>` for the step it belongs to, or as a new step.
- `git move` restacks every visible descendant of its target. Before you move, hide the old commits that a soft reset left visible, with `git hide -r`.
- When the worktree holds edits that are not yours, stage only your own hunks, with `git apply --cached`.
- Never run a bare `git stash`. All worktrees share one stash, so set work aside in a temporary commit.
- A conflicting restack may use `git restack --merge` while nobody else works in the worktree.
- After you rewrite the stack, build and run the tests at most once, at the new tip. The hooks still run at each commit.
- Move the target branch with `git branch -f`.

## Commit messages

A message has the shape that the `commit-msg` hooks check. Its scope is the project that the commit changes. A commit that changes only root files has no scope.

## Hooks

`git amend`, `git reword`, and `git move` run neither the `pre-commit` nor the `commit-msg` hooks. To run both at each commit of the stack, in a worktree that git-branchless manages, so the build of the working copy stays current:

    git test run -s worktree --no-cache -x 'f=$(mktemp) && git log -1 --format=%B > "$f" && pre-commit run --from-ref HEAD~ --to-ref HEAD && pre-commit run --hook-stage commit-msg --commit-msg-filename "$f"' '<target>..HEAD'

## Worktrees

Create a worktree only with `tools/worktree-add.sh`, and remove such a worktree only with `tools/worktree-remove.sh`. Never run vcpkg in a worktree.
