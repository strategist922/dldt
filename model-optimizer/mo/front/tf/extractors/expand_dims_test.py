"""
 Copyright (c) 2018 Intel Corporation

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
"""

from mo.front.tf.extractors.expand_dims import tf_expand_dims_ext
from mo.utils.unittest.extractors import BaseExtractorsTestingClass


class ExpandDimsExtractorTest(BaseExtractorsTestingClass):
    @classmethod
    def setUpClass(cls):
        cls.patcher = 'mo.front.tf.extractors.expand_dims.tf_expand_dims_infer'

    def test_expand_dims(self):
        self.expected = {}
        self.res = tf_expand_dims_ext(pb=None)
        self.res["infer"](None)
        self.call_args = self.infer_mock.call_args
        self.expected_call_args = None
        self.compare()
