import dotenv from 'dotenv';
import { z } from 'zod';

dotenv.config();

const envSchema = z.object({
  DATABASE_URL: z.url(),
  PORT: z.coerce.number().int().min(1).max(65535).default(3001),
  NODE_ENV: z.enum(['development', 'production', 'test']).default('development'),
  FRONTEND_URL: z.url().default('http://localhost:5173'),
  LOG_LEVEL: z
    .enum(['error', 'warn', 'info', 'http', 'verbose', 'debug', 'silly'])
    .default('info'),
  // The SBC engine's own SIP identity (engine/settings.toml [sip] address/port).
  // Routes pointing here would make the SBC forward a call back to itself.
  SBC_SIP_ADDRESS: z.string().min(1).default('127.0.0.1'),
  SBC_SIP_PORT: z.coerce.number().int().min(1).max(65535).default(5060),
});

const parsed = envSchema.safeParse(process.env);

if (!parsed.success) {
  const details = parsed.error.issues
    .map((issue) => `  ${issue.path.join('.')}: ${issue.message}`)
    .join('\n');
  throw new Error(`Invalid environment configuration:\n${details}`);
}

export const env = {
  databaseUrl: parsed.data.DATABASE_URL,
  port: parsed.data.PORT,
  nodeEnv: parsed.data.NODE_ENV,
  frontendUrl: parsed.data.FRONTEND_URL,
  logLevel: parsed.data.LOG_LEVEL,
  sbcSipAddress: parsed.data.SBC_SIP_ADDRESS,
  sbcSipPort: parsed.data.SBC_SIP_PORT,
};