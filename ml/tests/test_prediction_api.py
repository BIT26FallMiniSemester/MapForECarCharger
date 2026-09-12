import io
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "src"))
from post_predictions_api import api_base, fetch_catalog_mappings, resolve_payloads


class Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class PredictionApiTest(unittest.TestCase):
    def test_mapping_pagination_and_resolution(self):
        pages = [
            [{"station_id": index + 4, "data_source": "SOURCE", "external_id": str(index + 1)}
             for index in range(1000)],
            [{"station_id": 1004, "data_source": "SOURCE", "external_id": "1001"}],
        ]

        def opener(request, timeout):
            self.assertEqual(request.headers["X-internal-key"], "secret")
            page = 1 if "page=1" in request.full_url else 2
            return Response(json.dumps({"code": 0, "data": {"items": pages[page - 1]}}).encode())

        mappings = fetch_catalog_mappings("http://backend:8000/api/v1", "secret", {"SOURCE"}, opener)
        entries = [{"station_ref": {"data_source": "SOURCE", "external_id": "1"},
                    "payload": {"horizon_hours": 1, "points": []}}]
        self.assertEqual(resolve_payloads(entries, mappings)[0]["station_id"], 4)
        self.assertEqual(api_base("http://backend:8000/api/v1"), "http://backend:8000/api/v1")

    def test_repeated_catalog_page_fails_instead_of_looping(self):
        items = [{"station_id": i + 1, "data_source": "SOURCE", "external_id": str(i)}
                 for i in range(1000)]
        calls = []
        def opener(request, timeout):
            calls.append(request.full_url)
            return Response(json.dumps({"code": 0, "data": {"items": items}}).encode())
        with self.assertRaisesRegex(ValueError, "did not advance"):
            fetch_catalog_mappings("http://backend:8000", "secret", {"SOURCE"}, opener)
        self.assertEqual(len(calls), 2)

    def test_invalid_catalog_id_rejected(self):
        def opener(request, timeout):
            return Response(b'{"code":0,"data":{"items":[{"station_id":0,"data_source":"SOURCE","external_id":"1"}]}}')
        with self.assertRaisesRegex(ValueError, "invalid backend station ID"):
            fetch_catalog_mappings("http://backend:8000", "secret", {"SOURCE"}, opener)


if __name__ == "__main__":
    unittest.main()
