---
name: commit_changes
description: Triggers when the user asks to commit changes or save progress to git.
---

# Instructions for Committing Changes

When the user asks you to commit changes, follow this workflow to ensure clean, consistent git history using the Conventional Commits specification.

## 1. Stage Changes
Always stage all modified and untracked files before committing, unless the user specifies otherwise:
- Run `git add .` to stage all changes.

## 2. Review Changes
Before writing the commit message, briefly review the staged changes (e.g., using `git diff --staged`) to understand what was modified.

## 3. Write the Commit Message
Use the Conventional Commits format for the commit message. 

### Format
`<type>[optional scope]: <description>`

`[optional body]`

### Types
Use one of the following types:
- `feat`: A new feature
- `fix`: A bug fix
- `docs`: Documentation only changes
- `style`: Changes that do not affect the meaning of the code (white-space, formatting, etc)
- `refactor`: A code change that neither fixes a bug nor adds a feature
- `perf`: A code change that improves performance
- `test`: Adding missing tests or correcting existing tests
- `chore`: Changes to the build process or auxiliary tools

### Scope
Because this project contains multiple distinct components, **always try to include a scope** to indicate which component was affected.
Common scopes include:
- `engine` (for changes in `engine/` or its dependencies)
- `backend` (for changes in `control-plane/backend/`)
- `frontend` (for changes in `control-plane/frontend/`)

**Note on General Changes:** If the change affects the repository globally (e.g., adding `.agents/skills`, updating root `.gitignore`, or project-wide configurations), **omit the scope entirely**.
Example: `chore: add commit_changes skill`

Example with scope: `feat(engine): add support for PRACK`

### Description and Body
- **Description**: Keep it direct, imperative, and lowercase (e.g., "add support for PRACK", not "Added support..."). **STRICT LIMIT: The entire title line (type + scope + description) MUST NOT exceed 50 characters.** Be concise and get straight to the point.
- **Body**: For trivial changes, omit the body. For non-trivial changes, include a brief bulleted list in the body explaining *what* was changed and *why*. Keep the body text direct and not too long. Do not just restate the code changes; explain the reasoning. Wrap lines at 72 characters.

## 4. Execute Commit
Run the commit command (e.g., `git commit -m "<title>" -m "<body>"`). If the message is long or complex, it's safer to write it to a temporary file and use `git commit -F <file>`. As always, wait for the user to approve the command before it executes.
