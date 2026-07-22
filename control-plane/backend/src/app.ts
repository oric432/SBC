import cors from 'cors';
import dotenv from 'dotenv';
import { StatusCodes } from 'http-status-codes';
import { logger } from './utils/logger';
import express from 'express';
import 'express-async-errors';

import { checkDbConnection } from './db/client';
import errorHandlerMiddleware from './middlewares/errorHandlerMiddleware';
import routesRouter from './routes/routesRouter';
import { sendError } from './utils/apiResponse';

// Load environment variables
dotenv.config();

const app = express();
const PORT = process.env.PORT || 3001;

// Middleware
app.use(cors({
  origin: process.env.FRONTEND_URL || 'http://localhost:5173',
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

app.listen(PORT, () => {
  logger.info(`Server started on port ${PORT}`);

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
