import { Router } from 'express';
import { createSipUser, deleteSipUser, getSipUsers, updateSipUser } from '../controllers/sipUsersController';
import { validateBody, validateParams } from '../middlewares/validation';
import { sipUserBodySchema, sipUserIdParamSchema, sipUserUpdateBodySchema } from '../schemas/sipUsersSchemas';

const router = Router();

router.get('/', getSipUsers);
router.post('/', validateBody(sipUserBodySchema), createSipUser);
router.put('/:id', validateParams(sipUserIdParamSchema), validateBody(sipUserUpdateBodySchema), updateSipUser);
router.delete('/:id', validateParams(sipUserIdParamSchema), deleteSipUser);

export default router;
