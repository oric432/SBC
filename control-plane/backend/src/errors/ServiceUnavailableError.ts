import { StatusCodes } from 'http-status-codes';
import CustomAPIError from './CustomApiError';

class ServiceUnavailableError extends CustomAPIError {
  statusCode: number;

  constructor(message: string) {
    super(message);
    this.statusCode = StatusCodes.SERVICE_UNAVAILABLE;
  }
}

export default ServiceUnavailableError;
