import winston from 'winston';

const flattenError = (err: Error): string =>
  err instanceof AggregateError
    ? [err.message || err.name, ...err.errors.map((e) => flattenError(e))].filter(Boolean).join('; ')
    : err.message || err.name;

const flattenStack = (err: Error): string =>
  err instanceof AggregateError
    ? [err.stack, ...err.errors.map((e) => flattenStack(e))].filter(Boolean).join('\n')
    : (err.stack ?? '');

const normalizeErrors = winston.format((info) => {
  // AggregateError has an own `.errors` property; winston's core Logger.log
  // copies it onto `info` but only merges the (empty, for AggregateError)
  // top-level `.message`/`.stack` — plain Errors are already handled correctly
  // by winston itself, so only touch the AggregateError case here.
  const errors = (info as { errors?: unknown }).errors;
  if (Array.isArray(errors) && errors.length > 0) {
    const flattened = errors
      .map((e) => (e instanceof Error ? flattenError(e) : String(e)))
      .join('; ');
    info.message = `${info.message} ${flattened}`.trim();
    info.stack = errors
      .map((e) => (e instanceof Error ? flattenStack(e) : ''))
      .filter(Boolean)
      .join('\n');
  }
  return info;
});

export const logger = winston.createLogger({
  level: process.env.LOG_LEVEL || 'info',
  format: winston.format.combine(
    winston.format.timestamp(),
    normalizeErrors(),
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
