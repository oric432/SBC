import chalk from 'chalk';

type LogLevel = 'info'|'warn'|'error'|'debug'|'success';

interface LogEntry {
  timestamp: string;
  level: LogLevel;
  message: string;
  data?: any;
}

class Logger {
  private getTimestamp(): string {
    return new Date().toISOString();
  }

  private formatMessage(entry: LogEntry): string {
    const {timestamp, level, message, data} = entry;

    let coloredLevel: string;
    switch (level) {
      case 'error':
        coloredLevel = chalk.red.bold('[ERROR]');
        break;
      case 'warn':
        coloredLevel = chalk.yellow.bold('[WARN]');
        break;
      case 'success':
        coloredLevel = chalk.green.bold('[SUCCESS]');
        break;
      case 'debug':
        coloredLevel = chalk.blue.bold('[DEBUG]');
        break;
      default:
        coloredLevel = chalk.cyan.bold('[INFO]');
    }

    const time = chalk.gray(timestamp);
    const msg = this.colorizeMessage(message, level);

    if (data) {
      return `${time} ${coloredLevel} ${msg}\n${
          chalk.gray(JSON.stringify(data, null, 2))}`;
    }

    return `${time} ${coloredLevel} ${msg}`;
  }

  private colorizeMessage(message: string, level: LogLevel): string {
    switch (level) {
      case 'error':
        return chalk.red(message);
      case 'warn':
        return chalk.yellow(message);
      case 'success':
        return chalk.green(message);
      case 'debug':
        return chalk.blue(message);
      default:
        return message;
    }
  }

  info(message: string, data?: any): void {
    console.log(this.formatMessage({
      timestamp: this.getTimestamp(),
      level: 'info',
      message,
      data,
    }));
  }

  success(message: string, data?: any): void {
    console.log(this.formatMessage({
      timestamp: this.getTimestamp(),
      level: 'success',
      message,
      data,
    }));
  }

  warn(message: string, data?: any): void {
    console.log(this.formatMessage({
      timestamp: this.getTimestamp(),
      level: 'warn',
      message,
      data,
    }));
  }

  error(message: string, error?: any): void {
    const errorData = error instanceof Error ?
        {message: error.message, stack: error.stack} :
        error;

    console.log(this.formatMessage({
      timestamp: this.getTimestamp(),
      level: 'error',
      message,
      data: errorData,
    }));
  }

  debug(message: string, data?: any): void {
    if (process.env.NODE_ENV === 'development') {
      console.log(this.formatMessage({
        timestamp: this.getTimestamp(),
        level: 'debug',
        message,
        data,
      }));
    }
  }

  http(method: string, path: string, status: number, duration?: string): void {
    const statusStr = status.toString();
    const statusColor = status >= 500 ? chalk.red(statusStr) :
        status >= 400                 ? chalk.yellow(statusStr) :
        status >= 300                 ? chalk.cyan(statusStr) :
                                        chalk.green(statusStr);

    const methodColor = this.getMethodColor(method);
    const timestamp = chalk.gray(this.getTimestamp());

    console.log(
        `${timestamp} ${chalk.magenta.bold('[HTTP]')} ` +
        `${methodColor(method.padEnd(7))} ${path} ` +
        `${statusColor} ${duration || ''}`);
  }

  private getMethodColor(method: string): (msg: string) => string {
    const upper = method.toUpperCase();
    switch (upper) {
      case 'GET':
        return chalk.blue;
      case 'POST':
        return chalk.green;
      case 'PUT':
        return chalk.yellow;
      case 'DELETE':
        return chalk.red;
      case 'PATCH':
        return chalk.cyan;
      default:
        return (msg: string) => msg;
    }
  }

  websocket(event: string, data?: any): void {
    const timestamp = chalk.gray(this.getTimestamp());
    console.log(
        `${timestamp} ${chalk.magenta.bold('[WS]')} ${chalk.cyan(event)}` +
        (data ? `\n${chalk.gray(JSON.stringify(data, null, 2))}` : ''));
  }
}

export const logger = new Logger();
