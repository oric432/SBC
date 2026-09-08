import { z } from 'zod';

import { SUPPORTED_CODECS } from '../types/sipRoutes';

const codecSchema = z.enum(SUPPORTED_CODECS).nullable().optional();

export const routeBodySchema = z.object({
  priority: z.coerce.number().int().min(1),
  uri: z.string().min(1),
  sip_address: z.string().min(1),
  port: z.coerce.number().int().min(1).max(65535),
  codec: codecSchema,
});

export const priorityParamSchema = z.object({
  priority: z.coerce.number().int().min(1),
});

export const swapRouteBodySchema = z.object({
  targetPriority: z.coerce.number().int().min(1),
  uri: z.string().min(1),
  sip_address: z.string().min(1),
  port: z.coerce.number().int().min(1).max(65535),
  codec: codecSchema,
});
