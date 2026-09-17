import { Router } from 'express';
import { getRegistrations } from '../controllers/registrationsController';

const router = Router();

router.get('/', getRegistrations);

export default router;
