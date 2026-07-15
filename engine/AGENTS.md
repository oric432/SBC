# AGENTS.md

Guidance for coding agents (any LLM tool) working in the `engine` component of the SBC project.

## Paths
- All code for this component resides inside the `engine/` directory (relative to the workspace root). Treat this directory as the root for all engine-related changes.

## Workflow Tips

### General Workflow
- **User Guidance**: Proactively communicate your plan and the reason for each step.
- **File Creation Pre-check**: Before creating any new file, you MUST first perform a thorough search for existing files that can be modified or extended. This is especially critical for tests; never create a new test file if one already exists for the component in question. Always add new tests to the existing test file.
- **Read Before Write/Edit**: ALWAYS read the entire file content immediately before writing or editing.
- **File Deletion**: NEVER perform actions that might delete files (either directly via e.g., `rm` or indirectly via e.g., `git clean`) without first asking the user for permission.

### Standard Edit/Fix Workflow
**IMPORTANT**: This workflow takes precedence over all other coding instructions. Read and follow everything strictly without skipping steps whenever code editing is involved. Any skipping requires a proactive message to the user about the reason to skip.

1. **Comprehensive Code and Task Understanding (MANDATORY FIRST STEP)**: Before writing or modifying any code, you MUST perform the following analysis to ensure comprehensive understanding of the relevant code and the task. This is a non-negotiable prerequisite for all coding tasks.
    a. **Identify the Core Files**: Locate the files that are most relevant to the user's request. All analysis starts from these files.
    b. **Conduct a Full Audit**: 
        i. Read the full source of EVERY core file. 
        ii. For each core file, summarize the control flow and ownership semantics. State the intended purpose of the core file.
    c. **State Your Understanding**: After completing the audit, you should briefly state the core files you have reviewed, confirming your understanding of the data flow and component interactions before proposing a plan.
    d. **Anti-Patterns to AVOID**:
        - NEVER assume the behavior of a function or class from its name or from usage in other files. ALWAYS read the source implementation.
        - ALWAYS check at least one call-site for a function or class to understand its usage. The context is as important as the implementation.

2. **Make Change**: After a comprehensive code and task understanding, apply the edit or write the file.
    - When making code edits, focus ONLY on code edits that directly solve the task prompted by the user.

3. **Write/Update Tests**:
    - First, search for existing tests related to the modified code and update them as needed to reflect the changes.
    - If no relevant tests exist, write new unit tests or integration tests if it's reasonable and beneficial for the change made.
    - If tests are deemed not applicable for a specific change (e.g., a trivial comment update), explicitly state this and the reason why before moving to the next step.

4. **Build**: ALWAYS build relevant targets after making edits.
    - **Read the README**: ALWAYS check the project `README.md` (e.g., `engine/README.md`) before building to understand the build commands and how the project is configured.
    - Build using standard CMake commands and available targets for this codebase.

5. **Fix compile errors**: ALWAYS follow these steps to fix compile errors.
    - ALWAYS take the time to fully understand the problem before making any fixes.
    - ALWAYS read at least one new file for each compile error.
    - ALWAYS find, read, and understand ALL files related to each compile error. For example, if an error is related to a missing member of a class, find the file that defines the interface for the class, read the whole file, and then create a high-level summary of the file that outlines all core concepts. Come up with a plan to fix the error.
    - ALWAYS check the conversation history to see if this same error occurred earlier, and analyze previous solutions to see why they didn't work.
    - NEVER make speculative fixes. You should be confident before applying any fix that it will work. If you are not confident, read more files.
    - NEVER ignore compiler warnings for the currently modified code. If during the build you see warnings related to other parts of the codebase, explicitly state it to the user.

6. **Test**: ALWAYS run relevant tests after a successful build. 
    - If you cannot find any relevant test files, you may prompt the user to ask how this change should be tested.

7. **Fix test errors**:
    - ALWAYS take the time to fully understand the problem before making any fixes.

8. **Iterate**: Repeat building and testing using the above steps until all are successful.

### Post-Change Checklist
After making any code changes, you MUST complete the following checklist before considering the task complete:
- [ ] **Run Format**: Run `make format` or `./format.sh` (or apply `clang-format` directly) to ensure the code complies with the project's formatting standard.
- [ ] **Run Static Analysis**: Run `clang-tidy` on the modified files to ensure no new warnings or errors are introduced.

### Coding Standards & Style
- **C++ Standard**: The project uses **C++26**, as defined in the `CMakeLists.txt`. Ensure all newly written code leverages modern C++26 features appropriately and remains compliant with it.
- **Class/Struct Definitions**: Each **class** must be defined in its own single file (one class per file). **Structs**, however, are fine to declare multiple in the same file.
- **Interface Naming**: All **interfaces** must start with the prefix `I` (e.g., `IRouter`, `IHandler`).
- **Configuration Updates**: If you make any changes to the configuration settings in the code, ALWAYS make sure to update the corresponding example configuration file (e.g., `settings-example.toml`) to keep it in sync.
- **Type Aliases**:
    - Centralize type aliases in a `types.hpp` file within a subcomponent (e.g., `engine/sm/types.hpp`, `engine/call/types.hpp`) ONLY when the alias is used across multiple components or files. Otherwise, keep them local to where they are defined.
    - Type alias naming should use `CamelCase` as defined in the `.clang-tidy` configuration.
- **Namespaces**: All newly defined types (classes, structs, type aliases) and functions MUST be placed inside the `SbcEngine` namespace. Do not pollute the global namespace.

## Error Handling Guidelines

When writing or modifying C++ code in the engine, adhere to the following error handling practices:

- **Prefer Error Codes**: Always prefer using APIs that return error codes instead of throwing exceptions. If an existing API does not provide an error code interface (e.g., it throws), wrap it in an error-code-style return type.
- **Use Project Standard**: Prefer the project-specific error types defined in `utils/error.hpp`.
- **Extend `std::error_code`**: If the code can generate errors that should be handled programmatically, extend the custom `std::error_code` category if one exists.
- **Consult the User for New Categories**: If a custom `std::error_code` category does not exist for the current domain and the code has many potential errors, ask the user before creating a new custom `std::error_code` category.
- **Consult the User for New Codes**: Ask the user for permission before adding new error enum values to an existing `std::error_category`.
- **Informative Unrecoverable Errors**: If errors are not recoverable but you want to provide more information, use `std::string_view` strictly for statically allocated string literals (e.g., as the error type in `std::expected<T, std::string_view>`). Do not use `std::string_view` for dynamically formatted strings to avoid dangling pointers.
