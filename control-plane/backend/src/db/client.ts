import dotenv from 'dotenv';
import { drizzle } from 'drizzle-orm/node-postgres';
import { Pool } from 'pg';

import { logger } from '../utils/logger';
import * as schema from './schema';

dotenv.config();

const pool = new Pool({
  connectionString: process.env.DATABASE_URL,
});

// Idle clients emit 'error' async (e.g. DB restart); without this listener
// node treats it as an uncaught exception and kills the process.
pool.on('error', (err) => {
  logger.error('Unexpected error on idle DB client', err);
});

export const db = drizzle(pool, { schema });

export const checkDbConnection = async (): Promise<boolean> => {
  try {
    await pool.query('SELECT 1');
    return true;
  } catch (err) {
    logger.error('Database connection check failed', err);
    return false;
  }
};
