import unittest
import bmemcached
import gevent

class MemcacheTests(unittest.TestCase):
    def setUp(self):
        self.server = '127.0.0.1:11211'
        self.client = None

    def tearDown(self):
        self.client.disconnect_all()

    def test_basic(self, id=0):
        self.client = bmemcached.Client(self.server)
        self.assertTrue(self.client.set('key1' + str(id), 'value1' + str(id)))
        self.assertEqual(self.client.get('key1' + str(id)), 'value1' + str(id))

        for x in range(100):
            self.assertTrue(self.client.set('key_' + str(id) + str(x), 'value_' + str(id) + str(x)))
            self.assertEqual(self.client.get('key_' + str(id) + str(x)), 'value_' + str(id) + str(x))

    def test_basic_parallel(self):
        jobs = []
        for i in range(0, 10):
            jobs.append(gevent.spawn(self.test_basic, i))

        gevent.joinall(jobs)

    def test_cas(self):
        self.client = bmemcached.Client(self.server)

        cas = 999
        self.assertTrue(self.client.cas('key1', 'value1', cas))
        self.assertEqual(self.client.get('key1'), 'value1')

        # Verify with original cas
        self.assertTrue(self.client.cas('key1', 'value2', cas))
        self.assertEqual(self.client.get('key1'), 'value2')

        # Test with new cas, should not have any effect
        cas = cas + 1
        self.assertFalse(self.client.cas('key1', 'value3', cas))
        self.assertEqual(self.client.get('key1'), 'value2')

    def test_cas_delete(self):
        self.client = bmemcached.Client(self.server)

        # Test with cas.
        cas = 999
        self.assertTrue(self.client.cas('key1', 'test1', cas))
        self.assertEqual(self.client.get('key1'), 'test1')
        
        # Key not deleted with wrong cas.
        self.assertFalse(self.client.delete('key1', cas=cas+1))
        self.assertEqual('test1', self.client.get('key1'))

        # Key delted with write cas.
        self.assertTrue(self.client.delete('key1', cas=cas))
        self.assertEqual(None, self.client.get('key1'))
