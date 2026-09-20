import { Router } from 'express';
import { getActiveCalls, getCallHistory } from '../controllers/callsController';
import { validateQuery } from '../middlewares/validation';
import { callHistoryQuerySchema } from '../schemas/callsSchemas';

const router = Router();

router.get('/', validateQuery(callHistoryQuerySchema), getCallHistory);
router.get('/active', getActiveCalls);

export default router;
