import { useState } from 'react';
import { Plus, Minus } from 'lucide-react';

const DynamicForm = () => {
  const [dynamicInputs, setDynamicInputs] = useState([{ id: 1, value: '' }]);

  const addInput = () => {
    const newId = Math.max(...dynamicInputs.map(input => input.id)) + 1;
    setDynamicInputs([...dynamicInputs, { id: newId, value: '' }]);
  };

  const removeInput = (id) => {
    if (dynamicInputs.length > 1) {
      setDynamicInputs(dynamicInputs.filter(input => input.id !== id));
    }
  };

  const handleDynamicInputChange = (id, value) => {
    setDynamicInputs(dynamicInputs.map(input => 
      input.id === id ? { ...input, value } : input
    ));
  };

  return (
    <div className="p-6 max-w-4xl mx-auto space-y-6">
      {/* Large top input */}
      <div className="space-y-2">
        <label htmlFor="topInput" className="block text-sm font-medium text-gray-700">
          Main Input
        </label>
        <input
          id="topInput"
          type="text"
          className="w-full p-3 border rounded-lg shadow-sm"
          placeholder="Enter main text"
        />
      </div>

      {/* Three smaller inputs in a row */}
      <div className="grid grid-cols-3 gap-4">
        {['First', 'Second', 'Third'].map((label, index) => (
          <div key={index} className="space-y-2">
            <label className="block text-sm font-medium text-gray-700">
              {label} Input
            </label>
            <input
              type="text"
              className="w-full p-2 border rounded-lg shadow-sm"
              placeholder={`Enter ${label.toLowerCase()} text`}
            />
          </div>
        ))}
      </div>

      {/* Dynamic input fields */}
      <div className="space-y-4">
        {dynamicInputs.map((input) => (
          <div key={input.id} className="flex items-center space-x-2">
            <input
              type="text"
              value={input.value}
              onChange={(e) => handleDynamicInputChange(input.id, e.target.value)}
              className="flex-grow p-2 border rounded-lg shadow-sm"
              placeholder="Dynamic input"
            />
            <button
              onClick={() => addInput()}
              className="p-2 text-green-600 hover:text-green-800"
            >
              <Plus size={20} />
            </button>
            <button
              onClick={() => removeInput(input.id)}
              className={`p-2 ${
                dynamicInputs.length > 1
                  ? 'text-red-600 hover:text-red-800'
                  : 'text-gray-300'
              }`}
              disabled={dynamicInputs.length <= 1}
            >
              <Minus size={20} />
            </button>
          </div>
        ))}
      </div>
    </div>
  );
};

export default DynamicForm;