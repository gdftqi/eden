package utils

import "encoding/json"

func ToJSON(obj any) string {
	jstr, _ := json.Marshal(obj)
	return string(jstr)
}
