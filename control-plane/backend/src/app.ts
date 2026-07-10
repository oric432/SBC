import cors from 'cors';
import dotenv from 'dotenv';
import { logger } from './utils/logger';
import express from 'express';

import errorHandlerMiddleware from './middlewares/errorHandlerMiddleware';

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

// Error handling middleware (must be last)
app.use(errorHandlerMiddleware);

// 404 handler
app.use('*', (req, res) => {
  res.status(404).json({ error: 'Route not found' });
});

app.listen(PORT, () => {
  logger.success(`Server started on port ${PORT}`);
});

export default app;
