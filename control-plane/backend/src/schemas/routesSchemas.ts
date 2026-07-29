import { z } from 'zod';

export const routeBodySchema = z.object({
  priority: z.coerce.number().int().min(1),
  uri: z.string().min(1),
  sip_address: z.string().min(1),
  port: z.coerce.number().int().min(1).max(65535),
  codec: z.string().nullable().optional(),
});

export const priorityParamSchema = z.object({
  priority: z.coerce.number().int().min(1),
});

export const swapRouteBodySchema = z.object({
  targetPriority: z.coerce.number().int().min(1),
  uri: z.string().min(1),
  sip_address: z.string().min(1),
  port: z.coerce.number().int().min(1).max(65535),
  codec: z.string().nullable().optional(),
});
