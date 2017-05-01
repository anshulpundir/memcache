import pytest
import subprocess
import time

@pytest.fixture(scope='session', autouse=True)
def start_memcache():
    p = subprocess.Popen(['../build/memcache', "-t", "8"], stdout=subprocess.PIPE, stderr=subprocess.PIPE);

    # allow process to start
    time.sleep(1)
    yield p

    # teardown.
    p.kill()
    p.wait()



