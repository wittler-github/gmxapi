"""Test gmx.md submodule"""

# We are generally using py.test so simpler assert statements just work.

# Question: can/should pytest handle MPI jobs? How should we test features that only make sense in MPI?
# I'm increasingly thinking that the CMake-managed C++ extension module should be managed separately than the setuptools
# primary module. Then we can just do standard things like using CTest and googletest for the more complicated stuff.

import logging
logging.getLogger().setLevel(logging.DEBUG)
# create console handler
ch = logging.StreamHandler()
ch.setLevel(logging.DEBUG)
# create formatter and add it to the handler
formatter = logging.Formatter('%(asctime)s:%(name)s:%(levelname)s: %(message)s')
ch.setFormatter(formatter)
# add the handlers to the logger
logging.getLogger().addHandler(ch)

import unittest
import pytest

import gmx
import gmx.core
from gmx.data import tpr_filename

try:
    from mpi4py import MPI
    withmpi_only = pytest.mark.skipif(not MPI.Is_initialized() or MPI.COMM_WORLD.Get_size() < 2,
                                      reason="Test requires at least 2 MPI ranks, but MPI is not initialized or too small.")
except ImportError:
    withmpi_only = pytest.mark.skip(reason="Test requires at least 2 MPI ranks, but mpi4py is not available.")

@pytest.mark.skip(reason="updating Context handling...")
@pytest.mark.usefixtures("cleandir")
class BindingsTestCase(unittest.TestCase):
    def test_APIObjectsFromTpr(self):
        apisystem = gmx.core.from_tpr(tpr_filename)
        assert isinstance(apisystem, gmx.core.MDSystem)
        assert hasattr(apisystem, 'launch')
        session = apisystem.launch()
        assert hasattr(session, 'run')
        session.run()
        # Test rerunability
        # system = gmx.System()
        # runner = gmx.runner.SimpleRunner()
        # runner._runner = apirunner
        # system.runner = runner
        # assert isinstance(system, gmx.System)
        # assert isinstance(system.runner, gmx.runner.Runner)
        # assert isinstance(system.runner._runner, gmx.core.SimpleRunner)
        # with gmx.context.DefaultContext(system.runner) as session:
        #     session.run()
    def test_SystemFromTpr(self):
        system = gmx.System._from_file(tpr_filename)
        system.run()
    def test_Extension(self):
        import pytest
        # Test attachment of external code
        system = gmx.System._from_file(tpr_filename)
        potential = gmx.core.TestModule()
        assert isinstance(potential, gmx.core.MDModule)
        system.add_potential(potential)

        assert hasattr(potential, "bind")
        generic_object = object()
        with pytest.raises(Exception) as exc_info:
            potential.bind(generic_object)
        assert str(exc_info).endswith("MDModule bind method requires properly named PyCapsule input.")

        with gmx.context.DefaultContext(system.workflow) as session:
            session.run()

@pytest.mark.usefixtures("cleandir")
@pytest.mark.usefixtures("caplog")
def test_simpleSimulation():
    """Load a work specification with a single TPR file and run."""
    # use case 1: simple high-level
    md = gmx.workflow.from_tpr(tpr_filename)
    gmx.run(md)

@pytest.mark.usefixtures("cleandir")
@pytest.mark.usefixtures("caplog")
@withmpi_only
def test_array_context():
    md = gmx.workflow.from_tpr(tpr_filename)
    context = gmx.context.ParallelArrayContext(md)
    with context as session:
        session.run()

@pytest.mark.skip("not implemented")
@pytest.mark.usefixtures("cleandir")
@pytest.mark.usefixtures("caplog")
@withmpi_only
def test_plugin(caplog):
    # Test attachment of external code
    md = gmx.workflow.from_tpr(tpr_filename)

    # Create a WorkElement for the potential
    #potential = gmx.core.TestModule()
    potential_element = gmx.workflow.WorkElement(namespace="gmx.core", operation="TestModule")
    potential_element.name = "test_module"
    before = md.workspec.elements[md.name]
    md.add_dependancy(potential_element)
    assert potential_element.name in md.workspec.elements
    assert potential_element.workspec is md.workspec
    after = md.workspec.elements[md.name]
    assert not before is after

    context = gmx.context.ParallelArrayContext(md)
    # \todo swallow warning about wide MPI context
    # \todo use pytest context managers to turn raised exceptions into assertions.
    with context as session:
        if context.rank == 0:
            print(context.work)
        session.run()


if __name__ == '__main__':
    unittest.main()
