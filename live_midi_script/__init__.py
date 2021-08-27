from __future__ import absolute_import, print_function, unicode_literals
from .PossumBox import PossumBox

def create_instance(c_instance):
    return PossumBox(c_instance)
