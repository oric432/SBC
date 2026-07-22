import winston from 'winston';

export const logger = winston.createLogger({
  level: process.env.LOG_LEVEL || 'info',
  format: winston.format.combine(
    winston.format.timestamp(),
    winston.format.colorize(),
    winston.format.errors({ stack: true }),
    winston.format.printf(({ timestamp, level, message, stack }) => {
      const isVerbose = ['verbose', 'debug'].includes(process.env.LOG_LEVEL || '');
      const details = isVerbose && stack ? `\n${stack}` : '';
      return `${timestamp} [${level}] ${message}${details}`;
    })
  ),
  transports: [
    new winston.transports.Console()
  ]
});
