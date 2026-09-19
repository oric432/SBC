import { z } from 'zod';

export const sipUserBodySchema = z.object({
  username: z.string().min(1),
  realm: z.string().min(1),
  password: z.string().min(1),
  enabled: z.boolean().optional().default(true),
});

// username/realm are immutable after creation: changing either without the
// plaintext password (which is never stored) would leave a stale ha1 nothing
// can compute back to a working credential. Delete and recreate to "rename".
export const sipUserUpdateBodySchema = z.object({
  password: z.string().min(1).optional(),
  enabled: z.boolean().optional(),
});

export const sipUserIdParamSchema = z.object({
  id: z.coerce.number().int().min(1),
});
