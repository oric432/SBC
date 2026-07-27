import cors from 'cors';
import { StatusCodes } from 'http-status-codes';
import { logger } from './utils/logger';
import express from 'express';
import 'express-async-errors';
import morgan from 'morgan';

import { env } from './config/env';
import { checkDbConnection } from './db/client';
import errorHandlerMiddleware from './middlewares/errorHandlerMiddleware';
import routesRouter from './routes/routesRouter';
import { sendError } from './utils/apiResponse';

const app = express();

// Per-request logging -- routed through winston so it shares the app's
app.use(morgan('tiny', { stream: { write: (message) => logger.debug(message.trim()) } }));

// Middleware
app.use(cors({
  origin: env.frontendUrl,
  credentials: true
}));

app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// API routes
app.use('/api/b2bua/routes', routesRouter);

// Error handling middleware (must be last)
app.use(errorHandlerMiddleware);

// 404 handler
app.use('*', (req, res) => {
  sendError(res, 'Route not found', StatusCodes.NOT_FOUND);
});

app.listen(env.port, () => {
  logger.info(`Server started on port ${env.port}`);

  checkDbConnection().then((connected) => {
    if (!connected) {
      logger.warn(
        'Database not reachable — DB-backed endpoints will return 503 until it is available',
      );
    } else {
      logger.info('Database connection OK');
    }
  });
});

process.on('unhandledRejection', (reason) => {
  logger.error('Unhandled promise rejection', reason);
});

export default app;
