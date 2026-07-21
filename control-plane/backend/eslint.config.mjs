// eslint.config.ts
import eslint from '@eslint/js';
import globals from 'globals';
import tseslint from 'typescript-eslint';

export default [
  // Ignore junk
  {ignores: ['dist/**', 'node_modules/**']},

  // Base JS + TS recommended
  eslint.configs.recommended,
  ...tseslint.configs.recommended,

  // Backend defaults (Node globals, TypeScript parser already provided by
  // tseslint)
  {
    files: ['**/*.ts'],
    languageOptions: {
      globals: {
        ...globals.node,  // backend, not browser
      },
    },
  },

  // 🔧 FINAL OVERRIDES — placed LAST so they win
  {
    rules: {
      '@typescript-eslint/no-explicit-any': 'off',
      // (optional) other prefs:
      'quotes': ['error', 'single'],
      'semi': ['error', 'always'],
    },
  },
];
