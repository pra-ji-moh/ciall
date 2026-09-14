// connectors/index.js; upgrade 12 — registers the built-in connector
// types, same shape as kernelRegistry.js registering built-in kernels.
// A future connector (a real database driver, a message queue
// consumer, a specific SaaS API) is a new file plus one registration
// call here, not a new bespoke ingestion pipeline threaded through
// every caller by hand.

import { registerConnectorType, createConnector, listConnectorTypes } from './connector.js';
import { createFileConnector } from './fileConnector.js';
import { createHttpConnector } from './httpConnector.js';
import { ingestToObjectType } from './ingest.js';
import { fuseRecords, filterForConsumer } from './fusion.js';

registerConnectorType('file', createFileConnector);
registerConnectorType('http', createHttpConnector);

export { createConnector, listConnectorTypes, ingestToObjectType, fuseRecords, filterForConsumer };
