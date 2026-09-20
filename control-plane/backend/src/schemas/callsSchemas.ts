import { z } from 'zod';

const callStatusSchema = z.enum(['Success', 'Failed', 'No Route Available', 'Blocked']);

// Express parses a repeated `status=` param into an array but a single one
// into a bare string; normalize both (and absence) to an array.
const statusListSchema = z.preprocess(
  (value) => (value === undefined ? [] : Array.isArray(value) ? value : [value]),
  z.array(callStatusSchema),
);

export const callHistoryQuerySchema = z.object({
  status: statusListSchema,
  sortField: z.enum(['timestamp', 'caller', 'callee']).default('timestamp'),
  sortDir: z.enum(['asc', 'desc']).default('desc'),
  page: z.coerce.number().int().min(1).default(1),
  pageSize: z.coerce.number().int().min(1).max(100).default(10),
});

export type CallHistoryQuery = z.infer<typeof callHistoryQuerySchema>;
